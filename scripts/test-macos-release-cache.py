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
                                                       [relative]), (1, 0))
        self.assertGreater(self.source.stat().st_mtime_ns, artifact.stat().st_mtime_ns)
        self.assertGreater(self.source.stat().st_mtime_ns, mini_artifact.stat().st_mtime_ns)
        cache.verify_checkout(self.root, self.head)

    def test_reuses_unchanged_inputs_but_refreshes_changed_content(self):
        relative = str(self.source.relative_to(self.root))
        old_time = time.time_ns() - 10_000_000_000
        os.utime(self.source, ns=(old_time, old_time))
        cache.record_rust_inputs(self.root, self.cargo, [relative], self.head)
        os.utime(self.source, None)  # A fresh checkout changes file times.
        with patch.object(cache.time, 'sleep'):
            self.assertEqual(cache.refresh_rust_inputs(self.root, [self.cargo], [relative]), (0, 1))
        self.assertEqual(self.source.stat().st_mtime_ns, old_time)
        self.source.write_text('pub fn value() -> u8 { 2 }\n')
        os.utime(self.source, ns=(old_time, old_time))  # Even a preserved time must not hide changes.
        with patch.object(cache.time, 'sleep'):
            self.assertEqual(cache.refresh_rust_inputs(self.root, [self.cargo], [relative]), (1, 0))
        self.assertGreater(self.source.stat().st_mtime_ns, old_time)

    def test_rejects_future_cache_timestamps(self):
        artifact = self.cargo / 'future'
        artifact.parent.mkdir(parents=True)
        artifact.touch()
        future = time.time() + 120
        os.utime(artifact, (future, future))
        with self.assertRaisesRegex(ValueError, 'timestamps'):
            cache.refresh_rust_inputs(self.root, [self.cargo], [str(self.source.relative_to(self.root))])

    def test_invalid_receipt_falls_back_to_rebuilding(self):
        self.cargo.mkdir()
        (self.cargo / cache.RECEIPT_NAME).write_text('{invalid')
        with patch.object(cache.time, 'sleep'):
            self.assertEqual(cache.refresh_rust_inputs(
                self.root, [self.cargo], [str(self.source.relative_to(self.root))]), (1, 0))

    def test_ocr_derived_inputs_restore_only_identical_contents(self):
        metal = self.root / 'snow-crates/crates/simd-paddle-ocr-rs/src/runtime/gpu/metal'
        metal.mkdir(parents=True)
        shader = metal / 'shader.metal'
        shader.write_text('old shader')
        native = self.root / 'build/native.a'
        native.parent.mkdir(parents=True)
        native.write_text('old library')
        rsp = self.root / 'build/snow-shot-macos-arm64-release/snow_shot/generated/snow-ocr-crash-Release.rsp'
        rsp.parent.mkdir(parents=True)
        rsp.write_text(str(native) + '\n-lc++\n')
        old_time = time.time_ns() - 10_000_000_000
        for path in (metal, shader, native, rsp):
            os.utime(path, ns=(old_time, old_time))
        relative = str(self.source.relative_to(self.root))
        cache.record_rust_inputs(self.root, self.cargo, [relative], self.head)
        for path in (metal, shader, native, rsp):
            os.utime(path, None)
        self.assertEqual(cache.restore_derived_inputs(self.root, self.cargo), 4)
        for path in (metal, shader, native, rsp):
            self.assertEqual(path.stat().st_mtime_ns, old_time)
        native.write_text('new library')
        shader.write_text('new shader')
        os.utime(native, ns=(old_time, old_time))
        os.utime(shader, ns=(old_time, old_time))
        with patch.object(cache.time, 'sleep'):
            cache.restore_derived_inputs(self.root, self.cargo)
        self.assertGreater(native.stat().st_mtime_ns, old_time)
        self.assertGreater(metal.stat().st_mtime_ns, old_time)

    def test_missing_or_external_ocr_input_never_restores(self):
        metal = self.root / 'snow-crates/crates/simd-paddle-ocr-rs/src/runtime/gpu/metal'
        metal.mkdir(parents=True)
        rsp = self.root / 'build/snow-shot-macos-arm64-release/snow_shot/generated/snow-ocr-crash-Release.rsp'
        rsp.parent.mkdir(parents=True)
        rsp.write_text('/dev/null\n')
        self.assertEqual(cache.derived_inputs(self.root), {})
        self.assertEqual(cache.restore_derived_inputs(self.root, self.cargo), 0)

    def test_ocr_archive_timestamp_is_ignored_but_member_changes_are_not(self):
        def archive(payload, timestamp):
            header = (b'bridge.o/       ' + str(timestamp).encode().ljust(12) +
                      b'0     ' + b'0     ' + b'100644  ' +
                      str(len(payload)).encode().ljust(10) + b'`\n')
            return b'!<arch>\n' + header + payload + (b'\n' if len(payload) % 2 else b'')

        metal = self.root / 'snow-crates/crates/simd-paddle-ocr-rs/src/runtime/gpu/metal'
        metal.mkdir(parents=True)
        native = self.root / 'build/snow-shot-macos-arm64-release/snow_shot/libsnow_ocr_diagnostics_bridge.a'
        native.parent.mkdir(parents=True)
        native.write_bytes(archive(b'old object', 100))
        rsp = native.parent / 'generated/snow-ocr-crash-Release.rsp'
        rsp.parent.mkdir()
        rsp.write_text(str(native) + '\n')
        old_time = time.time_ns() - 10_000_000_000
        os.utime(native, ns=(old_time, old_time))
        cache.record_rust_inputs(self.root, self.cargo, [str(self.source.relative_to(self.root))], self.head)
        native.write_bytes(archive(b'old object', 200))
        cache.restore_derived_inputs(self.root, self.cargo)
        self.assertEqual(native.stat().st_mtime_ns, old_time)
        native.write_bytes(archive(b'new object', 200))
        os.utime(native, ns=(old_time, old_time))
        with patch.object(cache.time, 'sleep'):
            cache.restore_derived_inputs(self.root, self.cargo)
        self.assertGreater(native.stat().st_mtime_ns, old_time)
        native.write_bytes(b'!<thin>\nunsupported')
        self.assertIsNone(cache.archive_digest_without_timestamps(native))
        native.write_bytes(archive(b'old object', 200)[:-1])
        self.assertIsNone(cache.archive_digest_without_timestamps(native))

    def test_rejects_inputs_outside_the_checkout(self):
        link = self.root / 'external-link'
        link.symlink_to('/dev/null')
        with self.assertRaisesRegex(ValueError, 'outside the checkout'):
            list(cache.tracked_files(self.root, [str(link.relative_to(self.root))]))


if __name__ == '__main__':
    unittest.main()
