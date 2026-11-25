#!/bin/bash
# Measurement builds of Booster for one board flavor: release and debug, each
# built by flash.sh --build-only into OUT_DIR with -fstack-usage and
# -fcallgraph-info=su so tools/dev/stackdepth.py can compute worst-case stack
# depth. Prints each ELF's sections, its flash use against Booster's 760K
# slot, the size of the generated web assets (fsdata_srv.c) and its heap room
# (__end__ to __HeapLimit).
#
#   tools/dev/measure_builds.sh <souffle|croissant> [OUT_DIR]
#
# Never touches tools/dev/builds/<flavor>-<type>, booster/build-*, build/ or
# any submodule: OUT_DIR is wiped and rebuilt from scratch, so the flags reach
# every file. Flags go in through CFLAGS on purpose: passing -DCMAKE_C_FLAGS
# replaces the Pico toolchain's own and nothing assembles. Ported from
# sidecartos.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
case "${1:-}" in souffle|croissant) FLAVOR="$1" ;; *) sed -n '9p' "$0" | sed 's/^# *//'; exit 2 ;; esac
OUT="${2:-$(mktemp -d)}"
mkdir -p "$OUT"
export FLASH_BUILDS_DIR="$OUT" CFLAGS="-fstack-usage -fcallgraph-info=su"
SLOT=$((760 * 1024))   # FLASH in booster/src/memmap_booster_rp2350.ld

for type in release debug; do
  rm -rf "${OUT:?}/$FLAVOR-$type"
  echo "=== $FLAVOR $type ==="
  "$HERE/flash.sh" "$FLAVOR" "$type" --build-only | grep -v '^Building'
  elf="$OUT/$FLAVOR-$type/booster/booster.elf"
  echo "booster.elf:"
  arm-none-eabi-size -A "$elf" | grep -E '^\.(text|rodata|data|bss)\s' | sed 's/^/  /'
  used=$(stat -f %z "$OUT/$FLAVOR-$type/booster/booster.bin" 2> /dev/null \
         || stat -c %s "$OUT/$FLAVOR-$type/booster/booster.bin")
  printf "  flash: %d of %d bytes (%d%%), %d free\n" "$used" "$SLOT" \
    $((used * 100 / SLOT)) $((SLOT - used))
  printf "  web assets (fsdata_srv.c): %d bytes\n" \
    "$(wc -c < "$REPO/booster/src/fsdata_srv.c" | tr -d ' ')"
  eval "$(arm-none-eabi-nm "$elf" | awk '$3 == "__end__" {print "end=0x" $1}
                                          $3 == "__HeapLimit" {print "limit=0x" $1}')"
  printf "  heap room (__end__ to __HeapLimit): %d bytes\n" $((limit - end))
done
echo "Outputs in $OUT; stack depth: tools/dev/stackdepth.py $OUT/$FLAVOR-<type>/booster roots main"
