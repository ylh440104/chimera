#!/bin/sh
set -u

OUT=build
CC=${CC:-cc}
CFLAGS="-O2 -g -Wall -Wextra -std=gnu11 -Iinclude"

mkdir -p "$OUT"

$CC $CFLAGS -o "$OUT/chmdump" tools/chmdump.c src/macho.c src/shim.c src/loader.c
$CC $CFLAGS -o "$OUT/chmrun" tools/chmrun.c src/macho.c src/shim.c src/loader.c
$CC $CFLAGS -o "$OUT/probe" tools/probe.c src/macho.c src/shim.c src/loader.c

echo "built $OUT/chmdump $OUT/chmrun $OUT/probe"