#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).with_name('macos-ccache-prefix.py')
SPEC = importlib.util.spec_from_file_location('macos_ccache_prefix', SCRIPT)
launcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launcher)


class CachedPrefixTests(unittest.TestCase):
    def test_cmake_clang_and_objcxx_pch_use_header_once(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('cmake_pch.hxx', 'cmake_pch.objcxx.hxx'):
                header = str(Path(directory) / name)
                Path(header).touch()
                arguments = ['clang++', '-Xclang', '-include-pch', '-Xclang', header + '.pch',
                             '-Xclang', '-include', '-Xclang', header, '-c', 'main.cpp']
                self.assertEqual(launcher.rewrite(arguments),
                                 ['clang++', '-include', header, '-c', 'main.cpp'])
                self.assertEqual(launcher.rewrite(['clang++', '-include-pch', header + '.pch']),
                                 ['clang++', '-include', header])

    def test_other_pch_and_generation_are_unchanged(self):
        for arguments in (['clang++', '-Xclang', '-include-pch', '-Xclang', '/tmp/custom.pch'],
                          ['clang++', '-x', 'c++-header', '-Xclang', '-emit-pch'],
                          ['clang++', '-include', 'custom.h', '-c', 'skip.cpp']):
            self.assertEqual(launcher.rewrite(arguments), arguments)

    def test_missing_generated_header_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'does not exist'):
            launcher.rewrite(['clang++', '-include-pch', '/missing/cmake_pch.hxx.pch'])


if __name__ == '__main__':
    unittest.main()
