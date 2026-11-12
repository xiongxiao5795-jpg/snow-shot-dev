#!/usr/bin/env python3
"""Restore verified OCR inputs after Ninja has built the native bridge."""

import importlib.util
import os
from pathlib import Path
import shutil
import sys


def is_ocr_build(arguments):
    return (bool(arguments) and arguments[0] == 'build' and
            any(arguments[i:i + 2] == ['--package', 'snow-ocr-process']
                for i in range(len(arguments) - 1)))


def prepare_ocr(environment):
    root = Path(environment['GITHUB_WORKSPACE']).resolve()
    expected_manifest = root / 'build/snow-shot-macos-arm64-release/snow_shot/generated/snow-ocr-crash-Release.rsp'
    if Path(environment['SNOW_CRASHPAD_LINK_FILE']).resolve() != expected_manifest:
        raise ValueError('Unexpected OCR link manifest')
    cargo_dir = Path(environment['CARGO_TARGET_DIR']).resolve()
    if cargo_dir != root / '.tools/release-cargo':
        raise ValueError('Unexpected release Cargo cache directory')
    script = root / 'scripts/macos-release-cache.py'
    spec = importlib.util.spec_from_file_location('snow_release_cache', script)
    if spec is None or spec.loader is None:
        raise ValueError('Cannot load release cache safety checks')
    cache = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cache)
    cache.verify_checkout(root, environment['GITHUB_SHA'])
    restored = cache.restore_derived_inputs(root, cargo_dir)
    print(f'Restored {restored} unchanged OCR inputs immediately before Cargo', flush=True)


def main():
    cargo = os.environ.get('SNOW_REAL_CARGO') or shutil.which('cargo')
    if not cargo:
        raise ValueError('Cargo executable was not found')
    real_cargo = Path(cargo)
    if not real_cargo.is_absolute() or real_cargo.resolve() == Path(__file__).resolve():
        raise ValueError('A distinct absolute Cargo executable is required')
    if is_ocr_build(sys.argv[1:]):
        prepare_ocr(os.environ)
    os.execv(real_cargo, [str(real_cargo), *sys.argv[1:]])


if __name__ == '__main__':
    try:
        main()
    except (KeyError, OSError, ValueError) as error:
        print(f'macOS Cargo cache launcher: {error}', file=sys.stderr)
        sys.exit(1)
