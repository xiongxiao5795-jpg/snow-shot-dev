#!/bin/bash
set -euo pipefail

# Run only on a provisioned CI runner; this builds tiny throwaway fixtures.
work="$(mktemp -d "${RUNNER_TEMP:?}/snow-release-cache-fixture.XXXXXX")"
trap 'rm -rf "$work"' EXIT
export CCACHE_DIR="$work/ccache"
export CCACHE_COMPILERCHECK=content
export CARGO_TARGET_DIR="$work/cargo-target"
export CARGO_TERM_COLOR=never
mkdir -p "$work/rust/snow-crates/cache-fixture/src"

cat > "$work/value.cpp" <<'CPP'
#include <cstdio>
int main() { std::puts("old"); }
CPP
sleep 2
ccache clang++ -c "$work/value.cpp" -o "$work/value.o"
rm "$work/value.o"
cp -R "$CCACHE_DIR" "$work/saved-ccache"
rm -rf "$CCACHE_DIR"
cp -R "$work/saved-ccache" "$CCACHE_DIR"
ccache clang++ -c "$work/value.cpp" -o "$work/value.o"
ccache --print-stats | awk '$1 == "direct_cache_hit" || $1 == "preprocessed_cache_hit" { hits += $2 } END { exit hits > 0 ? 0 : 1 }'
cat > "$work/value.cpp" <<'CPP'
#include <cstdio>
int main() { std::puts("new"); }
CPP
ccache clang++ -c "$work/value.cpp" -o "$work/value.o"
clang++ "$work/value.o" -o "$work/cpp-fixture"
[[ "$("$work/cpp-fixture")" == new ]]

manifest="$work/rust/snow-crates/cache-fixture/Cargo.toml"
source="$work/rust/snow-crates/cache-fixture/src/main.rs"
cat > "$manifest" <<'TOML'
[package]
name = "snow-release-cache-fixture"
version = "0.1.0"
edition = "2024"
TOML
printf 'fn main() { println!("old"); }\n' > "$source"
cargo build --offline --release --manifest-path "$manifest"
git init -q "$work/rust"
git -C "$work/rust" add snow-crates
git -C "$work/rust" -c user.name=Fixture -c user.email=fixture@example.invalid commit -qm old
old_sha=$(git -C "$work/rust" rev-parse HEAD)
python3 "$GITHUB_WORKSPACE/scripts/macos-release-cache.py" record \
    --root "$work/rust" --expected "$old_sha" --cargo-dir "$CARGO_TARGET_DIR"
cp -Rp "$CARGO_TARGET_DIR" "$work/saved-cargo"
rm -rf "$CARGO_TARGET_DIR"
cp -Rp "$work/saved-cargo" "$CARGO_TARGET_DIR"
touch "$source"
python3 "$GITHUB_WORKSPACE/scripts/macos-release-cache.py" prepare \
    --root "$work/rust" --expected "$old_sha" --cargo-dir "$CARGO_TARGET_DIR"
cargo build --offline --release --verbose --manifest-path "$manifest" 2> "$work/cargo-warm.log"
cat "$work/cargo-warm.log"
grep -F 'Fresh snow-release-cache-fixture' "$work/cargo-warm.log"
printf 'fn main() { println!("new"); }\n' > "$source"
git -C "$work/rust" add snow-crates
git -C "$work/rust" -c user.name=Fixture -c user.email=fixture@example.invalid commit -qm new
new_sha=$(git -C "$work/rust" rev-parse HEAD)
python3 - "$source" "$CARGO_TARGET_DIR/.snow-source-receipt.json" <<'PY'
import json, os, sys
receipt = json.load(open(sys.argv[2]))
stamp = receipt['files']['snow-crates/cache-fixture/src/main.rs']['mtime_ns']
os.utime(sys.argv[1], ns=(stamp, stamp))
PY
python3 "$GITHUB_WORKSPACE/scripts/macos-release-cache.py" prepare \
    --root "$work/rust" --expected "$new_sha" --cargo-dir "$CARGO_TARGET_DIR"
cargo build --offline --release --manifest-path "$manifest"
[[ "$("$CARGO_TARGET_DIR/release/snow-release-cache-fixture")" == new ]]
printf 'C++ and Rust caches reused unchanged inputs and rebuilt changed inputs.\n'
