#!/bin/zsh

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-debug"

echo "Clearing build directory: $BUILD_DIR"
rm -rf "$BUILD_DIR"

echo "Configuring project"
if ! cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"; then
  echo "RESULT: configure failed"
  exit 1
fi

echo "Building PeanutButterUltimaFormatExampleTest"
if ! cmake --build "$BUILD_DIR" --config Debug --target PeanutButterUltimaFormatExampleTest -j4; then
  echo "RESULT: build failed"
  exit 1
fi

echo "Running PeanutButterUltimaFormatExampleTest"
if ctest --test-dir "$BUILD_DIR" --output-on-failure -R PeanutButterUltimaFormatExampleTest; then
  echo "RESULT: PASS"
  exit 0
fi

echo "RESULT: FAIL"
exit 1
