#!/bin/bash
set -euo pipefail

# Install SDK needed for building
git submodule init
git submodule update --init --recursive

# Update core firmware submodule to latest main branch
# and verify that the working tree points exactly to origin/main.
echo "Updating rp2-atarist-rpikb submodule to latest main branch..."
git submodule sync -- rp2-atarist-rpikb
git submodule set-branch --branch main rp2-atarist-rpikb
# Ensure submodule exists locally before direct git operations.
git submodule update --init rp2-atarist-rpikb
# Explicit fetch + checkout avoids stale detached HEAD confusion.
git -C rp2-atarist-rpikb fetch --prune origin main
git -C rp2-atarist-rpikb checkout --detach FETCH_HEAD

SUBMODULE_HEAD=$(git -C rp2-atarist-rpikb rev-parse HEAD)
ORIGIN_MAIN_HEAD=$(git -C rp2-atarist-rpikb rev-parse origin/main)
if [ "$SUBMODULE_HEAD" != "$ORIGIN_MAIN_HEAD" ]; then
  echo "ERROR: rp2-atarist-rpikb is not on latest origin/main"
  echo "  submodule HEAD: $SUBMODULE_HEAD"
  echo "  origin/main:   $ORIGIN_MAIN_HEAD"
  exit 1
fi

echo "rp2-atarist-rpikb HEAD: ${SUBMODULE_HEAD:0:7}"

# Pin the building versions
echo "Pinning the versions..."

# Copy the version.txt to each project
echo "Copy version.txt to each project"
cp version.txt booster/
cp version.txt placeholder/

# Display the version information
VERSION=$(tr -d '\r\n ' < version.txt)
echo "Version: $VERSION"

# Set the board type to be used for building
# If nothing passed as first argument, use pico2_w
BOARD_TYPE=${1:-pico2_w}
echo "Board type: $BOARD_TYPE"

# Set the release or debug build type
# If nothing passed as second argument, use release
BUILD_TYPE=${2:-release}
BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
echo "Build type: $BUILD_TYPE"

# Check if the third parameter is provided
RELEASE_TYPE=${3:-""}
echo "Release type: $RELEASE_TYPE"

# Set the board target used by firmware board-specific behavior.
# 1 -> Croissant Revision 2
# 2 -> Souffle Revision 2
BOARD_FLAVOR_RAW=${4:-souffle}
BOARD_FLAVOR=$(echo "$BOARD_FLAVOR_RAW" | tr '[:upper:]' '[:lower:]')
case "$BOARD_FLAVOR" in
  croissant|1)
    BOARD_TARGET=1
    BOARD_FLAVOR="croissant"
    ;;
  souffle|2)
    BOARD_TARGET=2
    BOARD_FLAVOR="souffle"
    ;;
  *)
    echo "Invalid board flavor '$BOARD_FLAVOR_RAW'. Use: croissant|souffle (or 1|2)."
    echo "Usage: ./build.sh <board_type> <build_type> [release_type] [board_flavor]"
    exit 1
    ;;
esac
echo "Board target: $BOARD_FLAVOR (BOARD_TARGET=$BOARD_TARGET)"

# Set the build directory. Delete previous contents if any
echo "Delete previous build directory"
rm -rf build
mkdir -p build

# Build core project
echo "Building core project"
(
  cd rp2-atarist-rpikb
  ./build.sh "$BOARD_TYPE" "$BUILD_TYPE" "$RELEASE_TYPE" "$BOARD_FLAVOR"
)

# Build the booster project
echo "Building booster project"
(
  cd booster
  ./build.sh "$BOARD_TYPE" "$BUILD_TYPE" "$RELEASE_TYPE" "$BOARD_FLAVOR"
)
if [ "$BUILD_TYPE_LOWER" = "release" ]; then
  BOOSTER_UF2="booster/dist/booster-$BOARD_TYPE-$BOARD_FLAVOR.uf2"
  if [ ! -f "$BOOSTER_UF2" ]; then
    BOOSTER_UF2="booster/dist/booster-$BOARD_TYPE.uf2"
  fi
else
  BOOSTER_UF2="booster/dist/booster-$BOARD_TYPE-$BOARD_FLAVOR-$BUILD_TYPE.uf2"
  if [ ! -f "$BOOSTER_UF2" ]; then
    BOOSTER_UF2="booster/dist/booster-$BOARD_TYPE-$BUILD_TYPE.uf2"
  fi
fi
cp "$BOOSTER_UF2" build/booster.uf2

# # Build the placeholder
# echo "Building placeholder project"
# (
#   cd placeholder
#   ./build.sh pico "$BUILD_TYPE"
# )
# if [ "$BUILD_TYPE_LOWER" = "release" ]; then
#   cp placeholder/dist/placeholder-pico.uf2 build/placeholder.uf2
# else
#   cp placeholder/dist/placeholder-pico-$BUILD_TYPE.uf2 build/placeholder.uf2
# fi

# # Build the UF2 combining the booster and placeholder
# mkdir -p dist
# echo "Building UF2 file combining booster and placeholder..."
# python build_uf2.py ./build/placeholder.uf2 ./build/booster.uf2 ./dist/rp-booster.uf2

# # Rename the file to include the version number and the build type
# if [ "$BUILD_TYPE_LOWER" = "release" ]; then
#   mv ./dist/rp-booster.uf2 ./dist/rp-booster-$VERSION.uf2
# else
#   mv ./dist/rp-booster.uf2 ./dist/rp-booster-$VERSION-$BUILD_TYPE.uf2
# fi

# If there is no third parameter, skip full image build
if [ -z "$RELEASE_TYPE" ]; then
  echo "Exiting now, no full image build requested"
  exit 0
fi

# Build full image
echo "Building full image file..."
rm -f ./dist/*.uf2
if [ "$BUILD_TYPE_LOWER" = "release" ]; then
  CORE_UF2="rp2-atarist-rpikb/dist/rp2-ikbd-$BOARD_TYPE-$BOARD_FLAVOR.uf2"
  if [ ! -f "$CORE_UF2" ]; then
    CORE_UF2="rp2-atarist-rpikb/dist/rp2-ikbd-$BOARD_TYPE.uf2"
  fi
  python merge_uf2.py "$CORE_UF2" ./build/booster.uf2 ./dist/rp-booster-all.uf2
  mv ./dist/rp-booster-all.uf2 ./dist/ikbd-booster-$VERSION-full.uf2
else
  CORE_UF2="rp2-atarist-rpikb/dist/rp2-ikbd-$BOARD_TYPE-$BOARD_FLAVOR-$BUILD_TYPE.uf2"
  if [ ! -f "$CORE_UF2" ]; then
    CORE_UF2="rp2-atarist-rpikb/dist/rp2-ikbd-$BOARD_TYPE-$BUILD_TYPE.uf2"
  fi
  python merge_uf2.py "$CORE_UF2" ./build/booster.uf2 ./dist/rp-booster-all.uf2
  mv ./dist/rp-booster-all.uf2 ./dist/ikbd-booster-$VERSION-$BUILD_TYPE-full.uf2
fi

# Done
echo "Done"

exit 0
