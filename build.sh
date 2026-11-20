#!/bin/sh
set -u

OUT=build
CC=${CC:-cc}
CFLAGS="-O2 -g -Wall -Wextra -std=gnu11 -Iinclude"

mkdir -p "$OUT"

$CC $CFLAGS -o "$OUT/chmdump" tools/chmdump.c src/*.c
$CC $CFLAGS -o "$OUT/chmrun" tools/chmrun.c src/*.c
$CC $CFLAGS -o "$OUT/probe" tools/probe.c src/*.c

echo "built $OUT/chmdump $OUT/chmrun $OUT/probe"