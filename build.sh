#!/bin/bash

# Install SDK needed for building
git submodule init
git submodule update --init --recursive

# Pin the building versions
echo "Pinning the  versions..."
cd rp2-atarist-rpikb
#git checkout tags/2.2.0
cd ..

# Copy the version.txt to each project
echo "Copy version.txt to each project"
cp version.txt booster/
cp version.txt placeholder/

# Display the version information
export VERSION=$(cat version.txt)
echo "Version: $VERSION"

# Check if the third parameter is provided
export RELEASE_TYPE=${3:-""}
echo "Release type: $RELEASE_TYPE"

# Set the board type to be used for building
# If nothing passed as first argument, use sidecartos_16mb
export BOARD_TYPE=${1:-pico_w}
echo "Board type: $BOARD_TYPE"

# Set the release or debug build type
# If nothing passed as second argument, use release
export BUILD_TYPE=${2:-release}
echo "Build type: $BUILD_TYPE"

# Set the build directory. Delete previous contents if any
echo "Delete previous build directory"
rm -rf build
mkdir build

# Build the booster project
echo "Building booster project"
cd booster
./build.sh $BOARD_TYPE $BUILD_TYPE
if [ "$BUILD_TYPE" = "release" ]; then
    cp  ./dist/booster-$BOARD_TYPE.uf2 ../build/booster.uf2
else
    cp  ./dist/booster-$BOARD_TYPE-$BUILD_TYPE.uf2 ../build/booster.uf2
fi
cd ..

# Build the placeholder
echo "Building placeholder project"
cd placeholder
./build.sh pico $BUILD_TYPE
if [ "$BUILD_TYPE" = "release" ]; then
    cp  ./dist/placeholder-pico.uf2 ../build/placeholder.uf2
else
    cp  ./dist/placeholder-pico-$BUILD_TYPE.uf2 ../build/placeholder.uf2
fi
cd ..

# Build the UF2 combining the booster and placeholder
mkdir -p dist
echo "Building UF2 file combining booster and placeholder..."
python build_uf2.py ./build/placeholder.uf2 ./build/booster.uf2 ./dist/rp-booster.uf2

# Rename the file to include the version number and the build type
if [ "$BUILD_TYPE" = "release" ]; then
    mv ./dist/rp-booster.uf2 ./dist/rp-booster-$VERSION.uf2
else
    mv ./dist/rp-booster.uf2 ./dist/rp-booster-$VERSION-$BUILD_TYPE.uf2
fi

# If there is no parameter ${3} passed, then exit
if [ -z ${3} ]; then
    echo "Exiting now, no image file building requested"
    exit 0
fi

# Build the placeholder
echo "Building core project"
cd rp2-atarist-rpikb

./build.sh pico_w $BUILD_TYPE
if [ "$BUILD_TYPE" = "release" ]; then
    cp  ./dist/rp2-ikbd-pico_w.uf2 ../dist/rp2-ikbd-pico_w.uf2
else
    cp  ./dist/rp2-ikbd-pico_w-$BUILD_TYPE.uf2 ../dist/rp2-ikbd-pico_w-$BUILD_TYPE.uf2
fi
cd ..

# Build the image file
echo "Building image file..."
python merge_uf2.py ./dist/rp2-ikbd-pico_w.uf2 ./build/booster.uf2 ./dist/rp-booster-all.uf2

# Rename the file to include the version number and the build type
if [ "$BUILD_TYPE" = "release" ]; then
    mv ./dist/rp-booster-all.uf2 ./dist/ikbd-booster-$VERSION-full.uf2
else
    mv ./dist/rp-booster-all.uf2 ./dist/ikbd-booster-$VERSION-$BUILD_TYPE-full.uf2
fi
 
# Done
echo "Done"

exit 0

