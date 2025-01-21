#!/bin/bash

BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

START_TIME="$SECONDS"
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake --fresh

# To get avoid of make's Clock skew waring.
sleep 2
make
END_TIME="$SECONDS"
ELAPSED=$((END_TIME - START_TIME))

cd ..
echo "Build finished"
echo "Total build time: $((ELAPSED - 2)) seconds"
echo
