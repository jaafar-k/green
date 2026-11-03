#!/bin/bash
# Cross-compile green for Miyoo Mini (armhf) using Onion gcc + mini sysroot + poppler 0.84 headers.
# Prerequisites (paths used in the Cloud Agent build):
#   /home/ubuntu/miyoo-tc/onion          # onion_toolchain.tar.gz
#   /opt/mmiyoo -> mini toolchain         # mini_toolchain-v1.0.tar.gz
#   /tmp/miyoo-dl/poppler-inc             # staged poppler 0.84.0 glib headers
#   /tmp/miyoo-dl/release/green/libs      # Steward poppler ARM libs for link
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CC="${CC:-/home/ubuntu/miyoo-tc/onion/bin/arm-linux-gnueabihf-gcc}"
STRIP="${STRIP:-/home/ubuntu/miyoo-tc/onion/bin/arm-linux-gnueabihf-strip}"
SYSROOT="${SYSROOT:-/opt/mmiyoo/arm-buildroot-linux-gnueabihf/sysroot}"
POPPLER_INC="${POPPLER_INC:-/tmp/miyoo-dl/poppler-inc}"
POPPLER_LIB="${POPPLER_LIB:-/tmp/miyoo-dl/release/green/libs}"
OUT="${OUT:-$ROOT/build-miyoo}"
mkdir -p "$OUT"
CFLAGS="${CFLAGS:--Os -Wall}"
CONFIG_FLAGS=(
  '-DGREEN_SYSCONFIG_FILE="/usr/local/etc/green.conf"'
  '-DGREEN_USERCONFIG_FILE=".green.conf"'
)
POPPLER_CFLAGS="-I$POPPLER_INC -I$SYSROOT/usr/include/glib-2.0 -I$SYSROOT/usr/lib/glib-2.0/include -I$SYSROOT/usr/include/cairo -I$SYSROOT/usr/include/pixman-1 -I$SYSROOT/usr/include/freetype2 -I$SYSROOT/usr/include/libpng16"
POPPLER_LIBS="-L$POPPLER_LIB -lpoppler-glib -L$SYSROOT/usr/lib -lgobject-2.0 -lglib-2.0 -lcairo"
SDL_CFLAGS="-I$SYSROOT/usr/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT"
SDL_LIBS="-L$SYSROOT/usr/lib -lSDL -lpthread"
cd "$ROOT"
$CC --sysroot=$SYSROOT "${CONFIG_FLAGS[@]}" $CFLAGS -c main.c $POPPLER_CFLAGS -o "$OUT/main.o"
$CC --sysroot=$SYSROOT $CFLAGS -c green.c $POPPLER_CFLAGS -o "$OUT/green.o"
$CC --sysroot=$SYSROOT $CFLAGS -c sdl.c $POPPLER_CFLAGS $SDL_CFLAGS -o "$OUT/sdl.o"
$CC --sysroot=$SYSROOT "$OUT/main.o" "$OUT/green.o" "$OUT/sdl.o" \
  $POPPLER_LIBS $SDL_LIBS \
  -Wl,-rpath-link,$POPPLER_LIB -Wl,-rpath-link,$SYSROOT/usr/lib \
  -o "$OUT/green"
$STRIP "$OUT/green"
file "$OUT/green"
