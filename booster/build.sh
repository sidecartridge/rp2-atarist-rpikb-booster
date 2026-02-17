#!/bin/bash
set -euo pipefail

# Down to main path
cd ..

# Install SDK needed for building
git submodule init
git submodule update --init --recursive

# Update submodules to latest remote state
echo "Updating submodules to latest remote state..."
if ! git submodule update --init --recursive --remote; then
    echo "Warning: submodule --remote update failed. Continuing with pinned versions."
fi

# Pin the building versions
echo "Pinning the SDK versions..."
cd pico-sdk
git checkout tags/2.2.0
cd ..

echo "Pinning the Extras SDK versions..."
cd pico-extras
git checkout tags/sdk-2.2.0
cd ..

echo "Pinning Bluepad32 submodule version..."
echo "Pinning Bluepad32 submodule version..."
cd bluepad32
#git checkout tags/4.2.0
#git checkout DIS-best-effort
git checkout No-DIS-Handler
cd ..

# Set the environment variables of the SDKs
export PICO_SDK_PATH=$PWD/pico-sdk
export PICO_EXTRAS_PATH=$PWD/pico-extras
export BLUEPAD32_ROOT=$PWD/bluepad32

# Return to booster path
cd booster

# Check if the third parameter is provided
export RELEASE_TYPE=${3:-""}
echo "Release type: $RELEASE_TYPE"

# Determine the file to use based on RELEASE_TYPE
if [ -z "$RELEASE_TYPE" ] || [ "$RELEASE_TYPE" = "final" ]; then
    VERSION_FILE="version.txt"
else
    VERSION_FILE="version-$RELEASE_TYPE.txt"
fi

# Read the release version from the version.txt file
export RELEASE_VERSION=$(cat "$VERSION_FILE" | tr -d '\r\n ')
echo "Release version: $RELEASE_VERSION"

# Get the release date and time from the current date
export RELEASE_DATE=$(date +"%Y-%m-%d %H:%M:%S")
echo "Release date: $RELEASE_DATE"

# Set the board type to be used for building
# If nothing passed as first argument, use pico2_w
export BOARD_TYPE=${1:-pico2_w}
export PICO_BOARD=$BOARD_TYPE
export PICO_PLATFORM=$BOARD_TYPE
echo "Board type: $BOARD_TYPE"

# Set the board target used by firmware board-specific behavior.
# 1 -> Croissant Revision 2
# 2 -> Souffle Revision 2
BOARD_FLAVOR_RAW=${4:-souffle}
BOARD_FLAVOR=$(echo "$BOARD_FLAVOR_RAW" | tr '[:upper:]' '[:lower:]')
case "$BOARD_FLAVOR" in
    croissant|1)
        export BOARD_TARGET=1
        BOARD_FLAVOR="croissant"
        ;;
    souffle|2)
        export BOARD_TARGET=2
        BOARD_FLAVOR="souffle"
        ;;
    *)
        echo "Invalid board flavor '$BOARD_FLAVOR_RAW'. Use: croissant|souffle (or 1|2)."
        echo "Usage: ./build.sh <board_type> <build_type> <release_type> <board_flavor>"
        exit 1
        ;;
esac
echo "Board target: $BOARD_FLAVOR (BOARD_TARGET=$BOARD_TARGET)"

# Set the release or debug build type
# If nothing passed as second argument, use release
export BUILD_TYPE=${2:-release}
echo "Build type: $BUILD_TYPE"
BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')

# If the build type is release, set DEBUG_MODE environment variable to 0
# Otherwise set it to 1
if [ "$BUILD_TYPE_LOWER" = "release" ]; then
    export DEBUG_MODE=0
    PRESET_KIND="release"
else
    export DEBUG_MODE=1
    PRESET_KIND="debug"
fi

# Resolve preset/build directory from board flavor + build type.
CONFIGURE_PRESET="${BOARD_FLAVOR}-${PRESET_KIND}"
BUILD_PRESET="${BOARD_FLAVOR}-${PRESET_KIND}"
BUILD_DIR="build-${BOARD_FLAVOR}-${PRESET_KIND}"
echo "Configure preset: $CONFIGURE_PRESET"
echo "Build preset: $BUILD_PRESET"

# We assume that the last firmware was built for the same board type
# And previously pushed to the repo version

# Build the project
echo "Building the project"
#export PICO_DEOPTIMIZED_DEBUG=1

# Set more environment variables for the build
export DISPLAY_ATARIST=1
export PICO_FLASH_ASSUME_CORE0_SAFE=1
export BOOSTER_DOWNLOAD_HTTPS=0

echo "DEBUG_MODE: $DEBUG_MODE"
echo "DISPLAY_ATARIST: $DISPLAY_ATARIST"
echo "PICO_FLASH_ASSUME_CORE0_SAFE: $PICO_FLASH_ASSUME_CORE0_SAFE"
echo "BOOSTER_DOWNLOAD_HTTPS: $BOOSTER_DOWNLOAD_HTTPS"

# Clean only the preset-specific build directory.
echo "Deleting previous preset build directory: $BUILD_DIR"
rm -rf "$BUILD_DIR"

(
    cd src
    cmake --preset "$CONFIGURE_PRESET"
    cmake --build --preset "$BUILD_PRESET"
)

# Copy the built firmware to the /dist folder
mkdir -p dist
echo "Copying the built firmware to the dist folder"
if [ "$BUILD_TYPE" = "release" ]; then
    cp "$BUILD_DIR/booster.uf2" "dist/booster-$BOARD_TYPE-$BOARD_FLAVOR.uf2"
else
    cp "$BUILD_DIR/booster.uf2" "dist/booster-$BOARD_TYPE-$BOARD_FLAVOR-$BUILD_TYPE.uf2"
fi
