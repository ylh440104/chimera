#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
runner=${1:?usage: run_darwin_abi.sh /path/to/chimera_run}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
for name in darwin_format darwin_dispatch darwin_foundation darwin_scene; do
    "${CLANG:-clang-18}" -target arm64-apple-ios15.0 -O2 -fno-objc-arc -fno-builtin -fno-stack-protector -c "$root/tests/$name.m" -o "$tmp/$name.o"
    "${LD64:-ld64.lld-18}" -arch arm64 -platform_version ios 15.0 15.0 -e _main -fixup_chains -undefined dynamic_lookup -o "$tmp/$name" "$tmp/$name.o"
    mkdir "$tmp/$name.root"
    frame=""
    if [ "$name" = darwin_scene ]; then
        cp "$root/tests/scene-info.plist" "$tmp/Info.plist"
        frame="$tmp/scene.argb"
    fi
    CHM_SANDBOX_ROOT="$tmp/$name.root" CHM_FRAME_OUT="$frame" timeout 20 "$runner" "$tmp/$name" >"$tmp/$name.log" 2>&1 || { rc=$?; cat "$tmp/$name.log" >&2; exit "$rc"; }
    if [ "$name" = darwin_scene ]; then
        grep -F 'name=Main & Window delegate=FixtureWindowDelegate' "$tmp/$name.log"
        grep -Fx 'ui: launched key_window=yes views=3' "$tmp/$name.log"
        python3 "$root/tests/check_scene_pixels.py" "$frame"
        if [ -n "${CHM_TEST_EXPORT:-}" ]; then
            mkdir -p "$CHM_TEST_EXPORT/Payload/SceneCheck.app"
            cp "$tmp/$name" "$CHM_TEST_EXPORT/Payload/SceneCheck.app/darwin_scene"
            cp "$tmp/Info.plist" "$CHM_TEST_EXPORT/Payload/SceneCheck.app/Info.plist"
            cp "$frame" "$tmp/$name.log" "$CHM_TEST_EXPORT/"
            (cd "$CHM_TEST_EXPORT" && zip -qr SceneCheck.ipa Payload)
        fi
    fi
    grep -Fx 'entry returned 0' "$tmp/$name.log"
    if [ "$name" = darwin_format ]; then
        grep -Fx 'out: guest:17:2.5' "$tmp/$name.log"
        printf 'guest=-7 1.25\n' | cmp - "$tmp/$name.root/mobile/Documents/profiles/abi.txt"
    fi
    printf '%s: passed\n' "$name"
done
