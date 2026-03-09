#!/usr/bin/env zsh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

BUILD_DIRS=(
  "$SCRIPT_DIR/build"
  "$SCRIPT_DIR/build-debug"
  "$SCRIPT_DIR/build-release"
)

for BUILD_DIR in "${BUILD_DIRS[@]}"; do
  if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
  fi
  echo "cleared: $BUILD_DIR"
done

cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build-release" -DCMAKE_BUILD_TYPE=Release
cmake --build "$SCRIPT_DIR/build-release" --config Release -j4

if cmake --build "$SCRIPT_DIR/build-release" --target PeanutButterUltimaCore PeanutButterUltima PeanutButterUltimaShell --config Release -j4; then
  echo "Built targets: PeanutButterUltimaCore, PeanutButterUltima, PeanutButterUltimaShell"
else
  echo "Failed to build one or more requested targets"
  exit 1
fi

echo "Release outputs:"
echo "  Library:    $SCRIPT_DIR/build-release/libPeanutButterUltimaCore.a"
echo "  Program:    $SCRIPT_DIR/build-release/PeanutButterUltima"
echo "  Shell:      $SCRIPT_DIR/build-release/PeanutButterUltimaShell"
