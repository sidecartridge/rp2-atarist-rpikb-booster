#!/bin/bash
# Build Booster out of tree for one board flavor, flash it through the Debug
# Probe, and confirm the RP runs exactly that build.
#
#   tools/dev/flash.sh <souffle|croissant> <release|debug>
#                      [--full] [--build-only] [--timeout S]
#
# Builds into tools/dev/builds/<flavor>-<type>/booster (incremental; never
# touches booster/build-*, build/, dist/ or any submodule) with the settings of
# booster/build.sh and its CMake presets: release is CMake Release, debug is
# CMake Debug with the 921,600 baud console. Release builds keep their symbols
# (BOOSTER_KEEP_SYMBOLS=ON) for the swd.py commands that need them; the .uf2 is
# the same as the product build's either way. --full also builds the core
# firmware (rp2-atarist-rpikb, at the commit the submodule pins) into
# tools/dev/builds/<flavor>-<type>/core with the settings of its build.sh.
#
# Everything builds against worktrees in tools/dev/sdk/, created on first use
# from the submodules' own history at the refs the build scripts pin
# (pico-sdk 2.2.0 with its libraries, pico-extras sdk-2.2.0, Bluepad32
# synthetic-HID-descriptor); Booster and the core firmware pin the same
# commits. The submodules themselves are never checked out. The ELFs are kept
# as tools/dev/builds/elf/<name>-<flavor>-<type>-<id>.elf for swd.py.
#
# Then flashes through the Debug Probe (tools/dev/swd.py program: only the
# ELFs' own flash, so the config sectors, the settings and the Bluetooth keys
# are kept; a watchdog reset after), boots into Booster (swd.py boot booster:
# the core firmware's boot selector reads BOOT_FEATURE), and checks over SWD,
# without the firmware's help: Booster runs, flash matches each ELF byte for
# byte, and Booster carries the new build ID. On failure exits 1 and prints
# the console since the last boot (tools/dev/console.py watch must be running
# for that).
#
# Environment: OPENOCD and PICO_OPENOCD_PATH (see swd.py), RELEASE_DATE
# (default: the date of the HEAD commit, so builds of one tree are
# byte-identical), FLASH_BUILDS_DIR (default tools/dev/builds: where the
# <flavor>-<type> build directories go; measure_builds.sh points it elsewhere).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$HERE/sdk"
ELF_DIR="$HERE/builds/elf"

usage() { sed -n '5,6p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }

[ $# -ge 2 ] || usage
FLAVOR="$1"; TYPE="$2"; shift 2
case "$FLAVOR" in
  croissant) BOARD_TARGET=1 ;;
  souffle)   BOARD_TARGET=2 ;;
  *) usage ;;
esac
case "$TYPE" in release|debug) ;; *) usage ;; esac
FULL=0; BUILD_ONLY=0; TIMEOUT=30
while [ $# -gt 0 ]; do
  case "$1" in
    --full) FULL=1 ;;
    --build-only) BUILD_ONLY=1 ;;
    --timeout) shift; TIMEOUT="${1:?}" ;;
    *) usage ;;
  esac
  shift
done

# --- worktrees ----------------------------------------------------------------
# Each pinned repository gets a worktree of its own history at the pinned ref,
# plus worktrees of the nested submodules the firmware compiles, at the commits
# that ref records. Everything comes from local history, except the first run
# in a fresh checkout, which clones the submodules from the network.
mkdir -p "$HERE/builds"
GIT_ERR="$HERE/builds/git.err"
git_retry() {  # the IDE's git can hold an index.lock for a moment
  local i
  for i in 1 2 3 4 5 6 7 8 9 10; do
    if "$@" 2> "$GIT_ERR"; then return 0; fi
    grep -q 'index.lock' "$GIT_ERR" || { cat "$GIT_ERR" >&2; return 1; }
    sleep 1
  done
  cat "$GIT_ERR" >&2; return 1
}
ensure_worktree() {  # ensure_worktree SUBMODULE REF MARKER_FILE [NESTED ...]
  local sub="$1" ref="$2" marker="$3"; shift 3
  local wt="$SDK_DIR/$sub-$ref" nested commit
  # A fresh checkout has not cloned the submodule: without it, git would read
  # this repository instead and fail with "invalid reference". Clone it once,
  # as build.sh does; this is the only step that needs the network.
  if [ ! -e "$REPO/$sub/.git" ]; then
    echo "Cloning the $sub submodule (once per checkout, from the network)"
    git_retry git -C "$REPO" submodule update --init "$sub"
  fi
  if [ ! -f "$wt/$marker" ]; then
    echo "Creating the $sub $ref worktree in ${wt#$REPO/}"
    mkdir -p "$SDK_DIR"
    git_retry git -C "$REPO/$sub" worktree add --detach --quiet "$wt" "$ref"
  fi
  for nested in "$@"; do
    [ -e "$wt/$nested/.git" ] && continue
    if [ ! -e "$REPO/$sub/$nested/.git" ]; then
      echo "Cloning $sub's $nested submodule (once per checkout, from the network)"
      git_retry git -C "$REPO/$sub" submodule update --init "$nested"
    fi
    commit="$(git -C "$REPO/$sub" ls-tree "$ref" "$nested" | awk '{print $3}')"
    if ! git -C "$REPO/$sub/$nested" cat-file -e "$commit^{commit}" 2> /dev/null; then
      echo "$sub $ref pins $nested at $commit, which is not in the local" \
           "history: run 'git -C $sub/$nested fetch' once" >&2
      exit 1
    fi
    echo "Adding $nested ($commit) to the $sub $ref worktree"
    rmdir "$wt/$nested" 2> /dev/null || true
    git_retry git -C "$REPO/$sub/$nested" worktree add --detach --quiet "$wt/$nested" "$commit"
  done
}

# The refs booster/build.sh and the core firmware's build.sh check out.
SDK_REF=2.2.0; EXTRAS_REF=sdk-2.2.0; BLUEPAD32_REF=synthetic-HID-descriptor
ensure_worktree pico-sdk "$SDK_REF" pico_sdk_init.cmake \
  lib/btstack lib/cyw43-driver lib/lwip lib/mbedtls lib/tinyusb
ensure_worktree pico-extras "$EXTRAS_REF" external/pico_extras_import.cmake
ensure_worktree bluepad32 "$BLUEPAD32_REF" src/components/bluepad32/CMakeLists.txt

# --- build ----------------------------------------------------------------------
export PICO_SDK_PATH="$SDK_DIR/pico-sdk-$SDK_REF"
export PICO_EXTRAS_PATH="$SDK_DIR/pico-extras-$EXTRAS_REF"
export BLUEPAD32_ROOT="$SDK_DIR/bluepad32-$BLUEPAD32_REF"
export BOARD_TYPE=pico2_w PICO_BOARD=pico2_w PICO_PLATFORM=pico2_w
export BOARD_TARGET RELEASE_TYPE=""
export RELEASE_VERSION="$(tr -d '\r\n ' < "$REPO/version.txt")"
export RELEASE_DATE="${RELEASE_DATE:-$(git -C "$REPO" log -1 --format=%cd --date=format:'%Y-%m-%d %H:%M:%S')}"
if [ "$TYPE" = release ]; then
  export DEBUG_MODE=0; CMAKE_TYPE=Release
else
  export DEBUG_MODE=1; CMAKE_TYPE=Debug
fi
mkdir -p "$ELF_DIR"
JOBS="$(sysctl -n hw.ncpu 2> /dev/null || nproc)"
OUT_BASE="${FLASH_BUILDS_DIR:-$HERE/builds}/$FLAVOR-$TYPE"

build() {  # build NAME SOURCE_DIR ELF_FILE [CMAKE_ARGS ...]
  local name="$1" src="$2" elf="$3"; shift 3
  local out="$OUT_BASE/$name"
  mkdir -p "$out"
  echo "Building $name ($FLAVOR $TYPE) in ${out#$REPO/}"
  cmake -S "$src" -B "$out" -DCMAKE_BUILD_TYPE=$CMAKE_TYPE "$@" \
    > "$out/cmake.log" 2>&1 || { tail -20 "$out/cmake.log"; return 1; }
  cmake --build "$out" -j"$JOBS" > "$out/make.log" 2>&1 \
    || { grep -m5 -E 'error|Error' "$out/make.log"; return 1; }
  [ -f "$out/$elf" ] || { echo "$name: no $elf in ${out#$REPO/}" >&2; return 1; }
}

# Booster, with the environment booster/build.sh exports.
(
  export DISPLAY_ATARIST=1 PICO_FLASH_ASSUME_CORE0_SAFE=1 BOOSTER_DOWNLOAD_HTTPS=0
  build booster "$REPO/booster/src" booster.elf -DBOOSTER_KEEP_SYMBOLS=ON
) || { echo "booster: build failed (${OUT_BASE#$REPO/}/booster/make.log)" >&2; exit 1; }
ID="$(sed -n 's/^#define RELEASE_BUILD_ID "\(.*\)"$/\1/p' \
      "$OUT_BASE/booster/generated/build_id/build_id.h")"
BOOSTER_ELF="$ELF_DIR/booster-$FLAVOR-$TYPE-$ID.elf"
cp "$OUT_BASE/booster/booster.elf" "$BOOSTER_ELF"
echo "  booster: $ID"
ELFS=("$BOOSTER_ELF")

if [ "$FULL" = 1 ]; then
  # The core firmware at the commit this repository pins, with the environment
  # its build.sh exports. Its ID is the core commit until it stamps its own.
  [ -e "$REPO/rp2-atarist-rpikb/src/CMakeLists.txt" ] \
    || git_retry git -C "$REPO" submodule update --init rp2-atarist-rpikb
  build core "$REPO/rp2-atarist-rpikb/src" rp2-ikbd.elf \
    || { echo "core: build failed (${OUT_BASE#$REPO/}/core/make.log)" >&2; exit 1; }
  CORE_ID="$(git -C "$REPO/rp2-atarist-rpikb" rev-parse --short=7 HEAD)"
  CORE_ELF="$ELF_DIR/core-$FLAVOR-$TYPE-$CORE_ID.elf"
  cp "$OUT_BASE/core/rp2-ikbd.elf" "$CORE_ELF"
  echo "  core: $CORE_ID"
  ELFS+=("$CORE_ELF")
fi
[ "$BUILD_ONLY" = 1 ] && exit 0

# --- flash and verify ---------------------------------------------------------
fail() {
  echo "FAILED: $1" >&2
  python3 "$HERE/console.py" since-boot 2> /dev/null | tail -40 >&2 || true
  exit 1
}
python3 "$HERE/swd.py" program "${ELFS[@]}" || fail "programming"
python3 "$HERE/swd.py" boot booster || fail "booting into Booster"
python3 "$HERE/swd.py" running "$BOOSTER_ELF" --timeout "$TIMEOUT" \
  || fail "the Booster just flashed is not running"
python3 "$HERE/swd.py" verify "${ELFS[@]}" || fail "flash differs from the ELF"
python3 "$HERE/swd.py" build-id "$BOOSTER_ELF" || fail "build ID mismatch"
echo "OK: $FLAVOR $TYPE flashed and verified"
