#!/usr/bin/env python3
"""Keep restored Cargo artifacts from making a release use stale source."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


RUST_INPUTS = ('snow-crates', 'snow_rust_ffi', 'snow_draw_engine_qt/Cargo.toml',
               'snow_draw_engine_qt/Cargo.lock', 'snow_draw_engine_qt/crates', 'snow_shot/rust')
RECEIPT_NAME = '.snow-source-receipt.json'


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


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def tracked_files(root, paths):
    for relative in paths:
        path = root / relative
        resolved = path.resolve()
        if not resolved.is_relative_to(root) or not resolved.is_file():
            raise ValueError(f'Rust input is missing or points outside the checkout: {relative}')
        yield relative, resolved


def refresh_rust_inputs(root, cargo_dirs, paths):
    """Make tracked Rust workspace inputs newer than every restored Cargo file.

    Cargo custom targets always invoke Cargo. Unchanged inputs get the time
    recorded after the previous successful build. Changed or unknown inputs
    get a time newer than every restored artifact, forcing reconsideration.
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
    receipt_path = cargo_dirs[0] / RECEIPT_NAME
    previous = {}
    if receipt_path.is_file() and not receipt_path.is_symlink():
        try:
            receipt = json.loads(receipt_path.read_text())
            if receipt.get('schema') == 1 and isinstance(receipt.get('files'), dict):
                previous = receipt['files']
        except (OSError, ValueError, AttributeError):
            pass  # Missing or invalid receipts safely force a rebuild.
    changed = unchanged = 0
    for relative, path in tracked_files(root, paths):
        entry = previous.get(relative)
        old_time = entry.get('mtime_ns') if isinstance(entry, dict) else None
        if (isinstance(old_time, int) and 0 < old_time <= now and
                entry.get('sha256') == digest(path)):
            os.utime(path, ns=(old_time, old_time))
            unchanged += 1
        else:
            os.utime(path, ns=(timestamp, timestamp))
            changed += 1
    if not changed and not unchanged:
        raise ValueError('No tracked Rust workspace inputs were found')
    # Avoid building while inputs have a future timestamp, which would cause
    # repeated Cargo rebuilds on filesystems with coarse timestamp precision.
    delay = (timestamp - time.time_ns()) / 1_000_000_000
    if changed and delay > 0:
        time.sleep(delay + 0.1)
    return changed, unchanged


def record_rust_inputs(root, cargo_dir, paths, commit):
    root = root.resolve()
    files = {relative: {'sha256': digest(path), 'mtime_ns': path.stat().st_mtime_ns}
             for relative, path in tracked_files(root, paths)}
    if not files:
        raise ValueError('No tracked Rust workspace inputs were found')
    cargo_dir.mkdir(parents=True, exist_ok=True)
    receipt = cargo_dir / RECEIPT_NAME
    temporary = cargo_dir / (RECEIPT_NAME + '.tmp')
    temporary.write_text(json.dumps({'schema': 1, 'commit': commit, 'files': files},
                                    sort_keys=True) + '\n')
    temporary.replace(receipt)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('prepare', 'record', 'verify'))
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--expected', required=True)
    parser.add_argument('--cargo-dir', type=Path, action='append')
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        verify_checkout(root, args.expected)
        if args.action in ('prepare', 'record'):
            if args.cargo_dir is None:
                raise ValueError('--cargo-dir is required for prepare and record')
            tracked = git(root, 'ls-files', '-z', '--recurse-submodules', '--', *RUST_INPUTS)
            paths = [os.fsdecode(item) for item in tracked.split(b'\0') if item]
            if args.action == 'prepare':
                changed, unchanged = refresh_rust_inputs(root, args.cargo_dir, paths)
                print(f'Checked {args.expected}; Rust inputs: {changed} changed, '
                      f'{unchanged} unchanged')
            else:
                record_rust_inputs(root, args.cargo_dir[0], paths, args.expected)
                print(f'Recorded Rust source fingerprints for {args.expected}')
        else:
            print(f'Verified clean checkout at {args.expected}')
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f'Release cache safety check failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
