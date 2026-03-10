#!/usr/bin/env zsh

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INPUT_DIR="$SCRIPT_DIR/input"

if [ -d "$INPUT_DIR" ]; then
  DS_STORE_COUNT="$(find "$INPUT_DIR" -type f -name '.DS_Store' | wc -l | tr -d '[:space:]')"
  if [ "$DS_STORE_COUNT" -gt 0 ]; then
    echo "Removing $DS_STORE_COUNT .DS_Store files from input"
    find "$INPUT_DIR" -type f -name '.DS_Store' -exec rm -f {} +
  fi
fi

BUILD_DIRS=(
  "$SCRIPT_DIR/build"
  "$SCRIPT_DIR/build-debug"
  "$SCRIPT_DIR/build-release"
  "$SCRIPT_DIR/build_release"
)

for BUILD_DIR in "${BUILD_DIRS[@]}"; do
  if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
  fi
done

echo "Configuring project"
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build-debug"
CONFIGURE_EXIT_CODE=$?
if [ "$CONFIGURE_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: CONFIGURE FAILED"
  exit "$CONFIGURE_EXIT_CODE"
fi

echo "Building PeanutButterUltimaGenerateExample"
cmake --build "$SCRIPT_DIR/build-debug" --config Debug --target PeanutButterUltimaGenerateExample -j4
BUILD_EXIT_CODE=$?
if [ "$BUILD_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: BUILD FAILED"
  exit "$BUILD_EXIT_CODE"
fi

echo "Running PeanutButterUltimaGenerateExample"
"$SCRIPT_DIR/build-debug/PeanutButterUltimaGenerateExample"
RUN_EXIT_CODE=$?
if [ "$RUN_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: RUN FAILED"
  exit "$RUN_EXIT_CODE"
fi

echo "RESULT: PASS"
exit 0
