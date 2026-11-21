#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
runner=${1:?usage: run_darwin_abi.sh /path/to/chimera_run}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
for name in darwin_format darwin_dispatch darwin_foundation; do
    "${CLANG:-clang-18}" -target arm64-apple-ios15.0 -O2 -fno-objc-arc -fno-builtin -fno-stack-protector -c "$root/tests/$name.m" -o "$tmp/$name.o"
    "${LD64:-ld64.lld-18}" -arch arm64 -platform_version ios 15.0 15.0 -e _main -fixup_chains -undefined dynamic_lookup -o "$tmp/$name" "$tmp/$name.o"
    mkdir "$tmp/$name.root"
    CHM_SANDBOX_ROOT="$tmp/$name.root" timeout 20 "$runner" "$tmp/$name" >"$tmp/$name.log" 2>&1 || { rc=$?; cat "$tmp/$name.log" >&2; exit "$rc"; }
    grep -Fx 'entry returned 0' "$tmp/$name.log"
    if [ "$name" = darwin_format ]; then
        grep -Fx 'out: guest:17:2.5' "$tmp/$name.log"
        printf 'guest=-7 1.25\n' | cmp - "$tmp/$name.root/mobile/Documents/profiles/abi.txt"
    fi
    printf '%s: passed\n' "$name"
done
