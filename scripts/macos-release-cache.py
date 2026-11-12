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


def archive_digest_without_timestamps(path):
    """Hash every archive byte except the volatile ar member timestamp fields.

    Reject unsupported or malformed archives instead of guessing equivalence.
    In particular, thin archives reference external files and are not accepted.
    """
    result = hashlib.sha256()
    try:
        with path.open('rb') as source:
            magic = source.read(8)
            if magic != b'!<arch>\n':
                return None
            result.update(magic)
            while header := source.read(60):
                if (len(header) != 60 or header[58:60] != b'`\n' or
                        not header[16:28].strip().isdigit() or
                        not header[48:58].strip().isdigit()):
                    return None
                size = int(header[48:58])
                result.update(header[:16] + b' ' * 12 + header[28:])
                remaining = size + size % 2
                while remaining:
                    block = source.read(min(1024 * 1024, remaining))
                    if not block:
                        return None
                    result.update(block)
                    remaining -= len(block)
    except OSError:
        return None
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
            if receipt.get('schema') in (1, 2) and isinstance(receipt.get('files'), dict):
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


def derived_inputs(root):
    """Fingerprint OCR build inputs recreated by a fresh CI checkout/configure."""
    root = root.resolve()
    metal = root / 'snow-crates/crates/simd-paddle-ocr-rs/src/runtime/gpu/metal'
    rsp = root / 'build/snow-shot-macos-arm64-release/snow_shot/generated/snow-ocr-crash-Release.rsp'
    if not metal.is_dir() or metal.is_symlink() or not rsp.is_file() or rsp.is_symlink():
        return {}
    paths = [rsp]
    for line in rsp.read_text().splitlines():
        candidate = Path(line.strip())
        if candidate.is_absolute():
            paths.append(candidate)
    entries = {}
    for path in paths:
        resolved = path.resolve()
        if not resolved.is_relative_to(root) or not resolved.is_file():
            return {}
        relative = str(resolved.relative_to(root))
        entry = {'sha256': digest(resolved), 'mtime_ns': resolved.stat().st_mtime_ns}
        if relative == ('build/snow-shot-macos-arm64-release/snow_shot/'
                        'libsnow_ocr_diagnostics_bridge.a'):
            normalized = archive_digest_without_timestamps(resolved)
            if normalized:
                entry['ar_without_timestamps_sha256'] = normalized
        entries[relative] = entry
    # Cargo scans directories named by rerun-if-changed recursively. Save a
    # fingerprint of every file and directory, including otherwise untracked files.
    tree = {}
    for directory, dirs, files in os.walk(metal, followlinks=False):
        current = Path(directory)
        if current.is_symlink() or any((current / name).is_symlink() for name in dirs + files):
            return {}
        relative = str(current.relative_to(root))
        tree[relative] = {'mtime_ns': current.stat().st_mtime_ns,
                          'children': sorted(dirs + files)}
        for name in files:
            path = current / name
            if not path.is_file():
                return {}
            tree[str(path.relative_to(root))] = {'mtime_ns': path.stat().st_mtime_ns,
                                                'sha256': digest(path)}
    return {'files': entries, 'metal': tree}


def restore_derived_inputs(root, cargo_dir):
    receipt = cargo_dir / RECEIPT_NAME
    if not receipt.is_file() or receipt.is_symlink():
        return 0
    try:
        old = json.loads(receipt.read_text()).get('derived', {})
        current = derived_inputs(root)
        if not old or not current:
            return 0
        previous_tree = old.get('metal', {})
        current_tree = current['metal']
        same_tree = current_tree.keys() == previous_tree.keys() and all(
            {k: v for k, v in value.items() if k != 'mtime_ns'} ==
            {k: v for k, v in previous_tree[key].items() if k != 'mtime_ns'}
            for key, value in current_tree.items())
    except (OSError, ValueError, TypeError, AttributeError, KeyError):
        return 0  # An incomplete receipt is a safe cache miss, before any mutation.
    count = 0
    changed = False
    timestamp = time.time_ns() + 1_000_000_000
    # Never backdate changed inputs. A failure after mutation propagates and
    # stops the build rather than risking reuse of an incomplete fingerprint.
    for relative, entry in current['files'].items():
        previous = old.get('files', {}).get(relative, {})
        stamp = previous.get('mtime_ns')
        same_bytes = entry.get('sha256') == previous.get('sha256')
        archive_digest = entry.get('ar_without_timestamps_sha256')
        same_archive = (archive_digest is not None and
                        archive_digest == previous.get('ar_without_timestamps_sha256'))
        if ((same_bytes or same_archive) and isinstance(stamp, int)
                and 0 < stamp <= time.time_ns()):
            os.utime(root / relative, ns=(stamp, stamp))
            count += 1
        else:
            os.utime(root / relative, ns=(timestamp, timestamp))
            changed = True
    if same_tree:
        for relative, entry in previous_tree.items():
            stamp = entry.get('mtime_ns')
            if isinstance(stamp, int) and 0 < stamp <= time.time_ns():
                os.utime(root / relative, ns=(stamp, stamp))
                count += 1
            else:
                os.utime(root / relative, ns=(timestamp, timestamp))
                changed = True
    else:
        metal = root / 'snow-crates/crates/simd-paddle-ocr-rs/src/runtime/gpu/metal'
        os.utime(metal, ns=(timestamp, timestamp))
        changed = True
    if changed:
        delay = (timestamp - time.time_ns()) / 1_000_000_000
        if delay > 0:
            time.sleep(delay + 0.1)
    return count


def record_rust_inputs(root, cargo_dir, paths, commit):
    root = root.resolve()
    files = {relative: {'sha256': digest(path), 'mtime_ns': path.stat().st_mtime_ns}
             for relative, path in tracked_files(root, paths)}
    if not files:
        raise ValueError('No tracked Rust workspace inputs were found')
    cargo_dir.mkdir(parents=True, exist_ok=True)
    receipt = cargo_dir / RECEIPT_NAME
    temporary = cargo_dir / (RECEIPT_NAME + '.tmp')
    temporary.write_text(json.dumps({'schema': 2, 'commit': commit, 'files': files,
                                     'derived': derived_inputs(root)},
                                    sort_keys=True) + '\n')
    temporary.replace(receipt)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('prepare', 'prepare-derived', 'record', 'verify'))
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--expected', required=True)
    parser.add_argument('--cargo-dir', type=Path, action='append')
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        verify_checkout(root, args.expected)
        if args.action in ('prepare', 'prepare-derived', 'record'):
            if args.cargo_dir is None:
                raise ValueError('--cargo-dir is required for prepare and record')
            tracked = git(root, 'ls-files', '-z', '--recurse-submodules', '--', *RUST_INPUTS)
            paths = [os.fsdecode(item) for item in tracked.split(b'\0') if item]
            if args.action == 'prepare-derived':
                restored = restore_derived_inputs(root, args.cargo_dir[0])
                print(f'Restored {restored} unchanged OCR derived inputs')
            elif args.action == 'prepare':
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
