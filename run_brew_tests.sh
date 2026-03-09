#!/usr/bin/env zsh

set -u

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
done

cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build-debug"
BUILD_EXIT_CODE=$?
if [ "$BUILD_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: BUILD CONFIGURE FAILED"
  exit "$BUILD_EXIT_CODE"
fi

cmake --build "$SCRIPT_DIR/build-debug" --config Debug -j4
BUILD_EXIT_CODE=$?
if [ "$BUILD_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: BUILD FAILED"
  exit "$BUILD_EXIT_CODE"
fi

TEST_OUTPUT_FILE="$SCRIPT_DIR/build-debug/ctest_brew_output.txt"
ctest --test-dir "$SCRIPT_DIR/build-debug" --output-on-failure | tee "$TEST_OUTPUT_FILE"
CTEST_EXIT_CODE=${pipestatus[1]}

FAILED_TEST_LINES="$(rg '^ *[0-9]+ - ' "$TEST_OUTPUT_FILE" 2>/dev/null || true)"
PASSED_SUMMARY_LINE="$(rg '^[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "$TEST_OUTPUT_FILE" 2>/dev/null || true)"

if [ "$CTEST_EXIT_CODE" -eq 0 ]; then
  echo "RESULT: PASS"
  if [ -n "$PASSED_SUMMARY_LINE" ]; then
    echo "$PASSED_SUMMARY_LINE"
  fi
  exit 0
fi

echo "RESULT: FAIL"
if [ -n "$FAILED_TEST_LINES" ]; then
  echo "FAILED TESTS:"
  echo "$FAILED_TEST_LINES"
fi
if [ -n "$PASSED_SUMMARY_LINE" ]; then
  echo "$PASSED_SUMMARY_LINE"
fi
exit "$CTEST_EXIT_CODE"
