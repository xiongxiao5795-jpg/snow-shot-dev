#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name('macos-release-cache.py')
SPEC = importlib.util.spec_from_file_location('macos_release_cache', SCRIPT)
cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cache)


class ReleaseCacheSafety(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='snow-release-cache-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.source = self.root / 'snow-crates/crates/example/src/lib.rs'
        self.source.parent.mkdir(parents=True)
        self.source.write_text('pub fn value() -> u8 { 1 }\n')
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'],
                       check=True)
        self.head = cache.git(self.root, 'rev-parse', 'HEAD').decode().strip()
        self.cargo = self.root / 'cargo-cache'

    def test_rejects_wrong_or_modified_checkout(self):
        cache.verify_checkout(self.root, self.head)
        with self.assertRaisesRegex(ValueError, 'does not match'):
            cache.verify_checkout(self.root, 'not-the-commit')
        self.source.write_text('pub fn value() -> u8 { 2 }\n')
        with self.assertRaisesRegex(ValueError, 'changes are present'):
            cache.verify_checkout(self.root, self.head)

    def test_restored_cargo_artifacts_are_older_than_workspace_inputs(self):
        artifact = self.cargo / 'aarch64-apple-darwin/release/.fingerprint/example/invoked.timestamp'
        artifact.parent.mkdir(parents=True)
        artifact.touch()
        mini_artifact = self.root / 'mini-cache/release/.fingerprint/mini/invoked.timestamp'
        mini_artifact.parent.mkdir(parents=True)
        mini_artifact.touch()
        newer = time.time() + 5
        os.utime(mini_artifact, (newer, newer))
        relative = str(self.source.relative_to(self.root))
        os.utime(self.source, (1, 1))
        with patch.object(cache.time, 'sleep'):
            self.assertEqual(cache.refresh_rust_inputs(self.root, [self.cargo, self.root / 'mini-cache'],
                                                       [relative]), 1)
        self.assertGreater(self.source.stat().st_mtime_ns, artifact.stat().st_mtime_ns)
        self.assertGreater(self.source.stat().st_mtime_ns, mini_artifact.stat().st_mtime_ns)
        cache.verify_checkout(self.root, self.head)

    def test_rejects_future_cache_timestamps(self):
        artifact = self.cargo / 'future'
        artifact.parent.mkdir(parents=True)
        artifact.touch()
        future = time.time() + 120
        os.utime(artifact, (future, future))
        with self.assertRaisesRegex(ValueError, 'timestamps'):
            cache.refresh_rust_inputs(self.root, [self.cargo], [str(self.source.relative_to(self.root))])


if __name__ == '__main__':
    unittest.main()
