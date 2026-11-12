#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch


SCRIPT = Path(__file__).with_name('macos-cargo-cache.py')
SPEC = importlib.util.spec_from_file_location('macos_cargo_cache', SCRIPT)
launcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launcher)


class CargoCacheLauncherTests(unittest.TestCase):
    def test_only_ocr_build_is_intercepted(self):
        self.assertTrue(launcher.is_ocr_build(['build', '--locked', '--package', 'snow-ocr-process']))
        self.assertFalse(launcher.is_ocr_build(['metadata', '--package', 'snow-ocr-process']))
        self.assertFalse(launcher.is_ocr_build(['build', '--package', 'snow-capture']))
        self.assertFalse(launcher.is_ocr_build(['build', '--package']))

    def test_other_cargo_commands_are_forwarded_unchanged(self):
        with patch.object(launcher.sys, 'argv', ['launcher', 'metadata', '--no-deps']), \
                patch.dict(launcher.os.environ, {'SNOW_REAL_CARGO': '/usr/bin/cargo'}), \
                patch.object(launcher.os, 'execv') as execute, \
                patch.object(launcher, 'prepare_ocr') as prepare:
            launcher.main()
            prepare.assert_not_called()
            execute.assert_called_once_with(Path('/usr/bin/cargo'),
                                            ['/usr/bin/cargo', 'metadata', '--no-deps'])

    def test_unexpected_manifest_or_cargo_directory_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            environment = {'GITHUB_WORKSPACE': str(root), 'SNOW_CRASHPAD_LINK_FILE': '/wrong',
                           'CARGO_TARGET_DIR': str(root / '.tools/release-cargo')}
            with self.assertRaisesRegex(ValueError, 'manifest'):
                launcher.prepare_ocr(environment)
            environment['SNOW_CRASHPAD_LINK_FILE'] = str(
                root / 'build/snow-shot-macos-arm64-release/snow_shot/generated/snow-ocr-crash-Release.rsp')
            environment['CARGO_TARGET_DIR'] = str(root / 'wrong')
            with self.assertRaisesRegex(ValueError, 'Cargo cache'):
                launcher.prepare_ocr(environment)

    def test_packaging_can_find_cargo_without_build_step_environment(self):
        with patch.object(launcher.sys, 'argv', ['launcher', 'metadata', '--no-deps']), \
                patch.dict(launcher.os.environ, {}, clear=True), \
                patch.object(launcher.shutil, 'which', return_value='/usr/bin/cargo'), \
                patch.object(launcher.os, 'execv') as execute, \
                patch.object(launcher, 'prepare_ocr') as prepare:
            launcher.main()
            prepare.assert_not_called()
            execute.assert_called_once_with(Path('/usr/bin/cargo'),
                                            ['/usr/bin/cargo', 'metadata', '--no-deps'])

    def test_missing_cargo_is_reported(self):
        with patch.dict(launcher.os.environ, {}, clear=True), \
                patch.object(launcher.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'Cargo executable was not found'):
                launcher.main()

    def test_checkout_is_verified_before_ocr_input_restoration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            environment = {
                'GITHUB_WORKSPACE': str(root), 'GITHUB_SHA': 'commit',
                'SNOW_CRASHPAD_LINK_FILE': str(root / 'build/snow-shot-macos-arm64-release/'
                                              'snow_shot/generated/snow-ocr-crash-Release.rsp'),
                'CARGO_TARGET_DIR': str(root / '.tools/release-cargo'),
            }
            events = []
            cache = MagicMock()
            cache.verify_checkout.side_effect = lambda *args: events.append(('verify', args))
            cache.restore_derived_inputs.side_effect = lambda *args: events.append(('restore', args)) or 1
            spec = MagicMock()
            with patch.object(launcher.importlib.util, 'spec_from_file_location', return_value=spec), \
                    patch.object(launcher.importlib.util, 'module_from_spec', return_value=cache):
                launcher.prepare_ocr(environment)
            self.assertEqual(events, [('verify', (root, 'commit')),
                                      ('restore', (root, root / '.tools/release-cargo'))])


if __name__ == '__main__':
    unittest.main()
