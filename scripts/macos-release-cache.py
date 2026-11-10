#!/usr/bin/env python3
"""Keep restored Cargo artifacts from making a release use stale source."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import time


RUST_INPUTS = ('snow-crates', 'snow_rust_ffi', 'snow_draw_engine_qt/Cargo.toml',
               'snow_draw_engine_qt/Cargo.lock', 'snow_draw_engine_qt/crates', 'snow_shot/rust')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def verify_checkout(root, expected):
    actual = git(root, 'rev-parse', 'HEAD').decode().strip()
    if actual != expected:
        raise ValueError(f'Checkout {actual} does not match requested commit {expected}')
    if git(root, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError('Tracked checkout or submodule changes are present')
    submodules = git(root, 'submodule', 'status', '--recursive').decode().splitlines()
    if any(line and line[0] != ' ' for line in submodules):
        raise ValueError('A submodule is missing, conflicted, or at the wrong commit')


def refresh_rust_inputs(root, cargo_dirs, paths):
    """Make tracked Rust workspace inputs newer than every restored Cargo file.

    Cargo custom targets always invoke Cargo. Refreshing inputs conservatively
    forces workspace crates and their build scripts to be reconsidered while
    leaving registry/git dependencies available from the cache.
    """
    root = root.resolve()
    latest = 0
    for cargo_dir in cargo_dirs:
        if cargo_dir.exists():
            for directory, _, files in os.walk(cargo_dir):
                for name in files:
                    path = Path(directory) / name
                    if not path.is_symlink():
                        latest = max(latest, path.stat().st_mtime_ns)
    now = time.time_ns()
    if latest > now + 30_000_000_000:
        raise ValueError('Restored Cargo cache contains timestamps more than 30s in the future')
    timestamp = max(now, latest) + 1_000_000_000
    count = 0
    for relative in paths:
        path = root / relative
        if path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(root):
            os.utime(path, ns=(timestamp, timestamp))
            count += 1
    if not count:
        raise ValueError('No tracked Rust workspace inputs were found')
    # Avoid building while inputs have a future timestamp, which would cause
    # repeated Cargo rebuilds on filesystems with coarse timestamp precision.
    delay = (timestamp - time.time_ns()) / 1_000_000_000
    if delay > 0:
        time.sleep(delay + 0.1)
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('prepare', 'verify'))
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--expected', required=True)
    parser.add_argument('--cargo-dir', type=Path, action='append')
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        verify_checkout(root, args.expected)
        if args.action == 'prepare':
            if args.cargo_dir is None:
                raise ValueError('--cargo-dir is required for prepare')
            tracked = git(root, 'ls-files', '-z', '--recurse-submodules', '--', *RUST_INPUTS)
            paths = [os.fsdecode(item) for item in tracked.split(b'\0') if item]
            count = refresh_rust_inputs(root, args.cargo_dir, paths)
            print(f'Checked {args.expected}; refreshed {count} Rust workspace inputs')
        else:
            print(f'Verified clean checkout at {args.expected}')
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f'Release cache safety check failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
