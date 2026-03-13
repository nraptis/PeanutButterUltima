#!/usr/bin/env zsh

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-test-release"
TEST_OUTPUT_FILE="$BUILD_DIR/codex_cases_output.txt"
DETAILED_LOG_FILE="$SCRIPT_DIR/run_brew_tests_detailed_logs.txt"
WINDOWS_BUILD_DIR="$SCRIPT_DIR/build-test-release-windows"
WINDOWS_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchains/mingw-w64-x86_64.cmake"
WINDOWS_ICON_ICO="$WINDOWS_BUILD_DIR/icon.ico"
WINDOWS_EXE_PATH=""
WINDOWS_BUILD_NOTE="not attempted"

BUILD_DIRS=(
  "$SCRIPT_DIR/build"
  "$SCRIPT_DIR/build-debug"
  "$SCRIPT_DIR/build-test-release"
  "$SCRIPT_DIR/build-test-release-windows"
  "$SCRIPT_DIR/build-release"
)

typeset -a PASSED_CASES
typeset -a FAILED_CASES
typeset -A FAILED_CASE_REASONS
typeset -A SEEN_CASES
typeset -a MISSING_RANDOM_CASES
typeset -i RANDOM_CASE_SEEN_COUNT=0

write_detailed_log() {
  local passed_count="$1"
  local failed_count="$2"
  local total_count="$3"
  local pass_percent="$4"
  local case_name reason

  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "generated_at: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "summary: ${passed_count} passed, ${failed_count} failed, ${total_count} total (${pass_percent}% passed)"
    echo ""

    if [ "${#MISSING_RANDOM_CASES[@]}" -gt 0 ]; then
      echo "Missing linked random test cases:"
      for case_name in "${MISSING_RANDOM_CASES[@]}"; do
        echo "  - ${case_name}"
      done
    else
      echo "No missing random test cases."
    fi

    echo ""
    if [ "${#FAILED_CASES[@]}" -gt 0 ]; then
      echo "Failed case-level tests:"
      for case_name in "${FAILED_CASES[@]}"; do
        reason="${FAILED_CASE_REASONS[$case_name]-}"
        if [ -n "$reason" ]; then
          echo "  - ${case_name} :: ${reason}"
        else
          echo "  - ${case_name}"
        fi
      done
    else
      echo "No failed case-level tests."
    fi

    echo ""
    if [ "${#PASSED_CASES[@]}" -gt 0 ]; then
      echo "Passed case-level tests:"
      for case_name in "${PASSED_CASES[@]}"; do
        echo "  - ${case_name}"
      done
    else
      echo "No passed case-level tests."
    fi

    echo ""
    echo "Raw codex output:"
    if [ -f "$TEST_OUTPUT_FILE" ]; then
      cat "$TEST_OUTPUT_FILE"
    else
      echo "No codex output captured."
    fi
  } > "$DETAILED_LOG_FILE"
}

collect_case_results() {
  local pLogFile="$1"
  local line case_name case_reason
  while IFS= read -r line; do
    case_name="$(echo "$line" | sed -nE 's/^\[PASS\][[:space:]]+(.+)$/\1/p')"
    if [ -n "$case_name" ]; then
      PASSED_CASES+=("$case_name")
      SEEN_CASES[$case_name]=1
      continue
    fi

    case_name="$(echo "$line" | sed -nE 's/^\[FAIL\][[:space:]]+([^:]+)([[:space:]]+::.*)?$/\1/p')"
    if [ -n "$case_name" ]; then
      case_reason="$(echo "$line" | sed -nE 's/^\[FAIL\][[:space:]]+[^:]+[[:space:]]+::[[:space:]]*(.*)$/\1/p')"
      FAILED_CASES+=("$case_name")
      SEEN_CASES[$case_name]=1
      if [ -n "$case_reason" ]; then
        FAILED_CASE_REASONS[$case_name]="$case_reason"
      fi
    fi
  done < "$pLogFile"
}

generate_windows_ico() {
  local source_image="$1"
  local output_ico="$2"
  python3 - "$source_image" "$output_ico" <<'PY'
import sys
from PIL import Image

src = sys.argv[1]
dst = sys.argv[2]
img = Image.open(src).convert("RGBA")
sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
img.save(dst, format="ICO", sizes=sizes)
PY
}

build_windows_codex_exe() {
  local icon_source=""
  local configure_rc build_rc
  local found_exe=""
  local -a cmake_config_args

  if [ ! -f "$WINDOWS_TOOLCHAIN_FILE" ]; then
    WINDOWS_BUILD_NOTE="skipped (missing toolchain file: $WINDOWS_TOOLCHAIN_FILE)"
    return 0
  fi
  if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    WINDOWS_BUILD_NOTE="skipped (install mingw-w64: brew install mingw-w64)"
    return 0
  fi
  if ! command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then
    WINDOWS_BUILD_NOTE="skipped (missing x86_64-w64-mingw32-windres from mingw-w64)"
    return 0
  fi

  mkdir -p "$WINDOWS_BUILD_DIR"
  if [ -f "$SCRIPT_DIR/icon.png" ]; then
    icon_source="$SCRIPT_DIR/icon.png"
  elif [ -f "$SCRIPT_DIR/icon.jpg" ]; then
    icon_source="$SCRIPT_DIR/icon.jpg"
  fi

  if [ -n "$icon_source" ]; then
    if ! generate_windows_ico "$icon_source" "$WINDOWS_ICON_ICO"; then
      WINDOWS_BUILD_NOTE="warning: failed to generate .ico from $icon_source (building .exe without embedded icon)"
      rm -f "$WINDOWS_ICON_ICO"
    fi
  fi

  cmake_config_args=(
    -S "$SCRIPT_DIR"
    -B "$WINDOWS_BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TOOLCHAIN_FILE="$WINDOWS_TOOLCHAIN_FILE"
    -DPEANUT_BUTTER_ULTIMA_BUILD_APP=OFF
    -DPEANUT_BUTTER_ULTIMA_BUILD_TESTS=ON
  )
  if [ -f "$WINDOWS_ICON_ICO" ]; then
    cmake_config_args+=("-DPB_WINDOWS_ICON=$WINDOWS_ICON_ICO")
  fi

  cmake "${cmake_config_args[@]}"
  configure_rc=$?
  if [ "$configure_rc" -ne 0 ]; then
    WINDOWS_BUILD_NOTE="failed (windows configure failed with exit code $configure_rc)"
    return 0
  fi

  cmake --build "$WINDOWS_BUILD_DIR" --config Release -j4 --target PeanutButterCodexTests
  build_rc=$?
  if [ "$build_rc" -ne 0 ]; then
    WINDOWS_BUILD_NOTE="failed (windows build failed with exit code $build_rc)"
    return 0
  fi

  found_exe="$(find "$WINDOWS_BUILD_DIR" -maxdepth 4 -type f -name 'PeanutButterCodexTests.exe' | head -n 1)"
  if [ -n "$found_exe" ]; then
    WINDOWS_EXE_PATH="$found_exe"
    if [ -f "$WINDOWS_ICON_ICO" ]; then
      WINDOWS_BUILD_NOTE="built with embedded icon"
    else
      WINDOWS_BUILD_NOTE="built (no icon source found)"
    fi
    return 0
  fi

  WINDOWS_BUILD_NOTE="failed (build completed but .exe was not found)"
  return 0
}

for BUILD_PATH in "${BUILD_DIRS[@]}"; do
  if [ -d "$BUILD_PATH" ]; then
    rm -rf "$BUILD_PATH"
  fi
done

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
BUILD_EXIT_CODE=$?
if [ "$BUILD_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: BUILD CONFIGURE FAILED"
  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "Build configure failed with exit code $BUILD_EXIT_CODE."
  } > "$DETAILED_LOG_FILE"
  exit "$BUILD_EXIT_CODE"
fi

cmake --build "$BUILD_DIR" --config Release -j4
BUILD_EXIT_CODE=$?
if [ "$BUILD_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: BUILD FAILED"
  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "Build failed with exit code $BUILD_EXIT_CODE."
  } > "$DETAILED_LOG_FILE"
  exit "$BUILD_EXIT_CODE"
fi

CODEX_BINARY="$BUILD_DIR/PeanutButterCodexTests"
if [ ! -x "$CODEX_BINARY" ]; then
  echo "RESULT: FAIL"
  echo "Missing codex binary: $CODEX_BINARY"
  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "Missing codex binary: $CODEX_BINARY"
  } > "$DETAILED_LOG_FILE"
  exit 1
fi

"$CODEX_BINARY" > "$TEST_OUTPUT_FILE" 2>&1
CODEX_EXIT_CODE=$?

collect_case_results "$TEST_OUTPUT_FILE"

RANDOM_CASE_SOURCE_COUNT=$(find "$SCRIPT_DIR/tests/random" -maxdepth 1 -type f -name 'Test*.cpp' | wc -l | tr -d '[:space:]')
for random_src in "$SCRIPT_DIR"/tests/random/Test*.cpp(N); do
  random_case_name="$(basename "$random_src" .cpp)"
  if [ -z "${SEEN_CASES[$random_case_name]-}" ]; then
    MISSING_RANDOM_CASES+=("$random_case_name")
  else
    RANDOM_CASE_SEEN_COUNT=$((RANDOM_CASE_SEEN_COUNT + 1))
  fi
done

TOTAL_CASE_COUNT=$(( ${#PASSED_CASES[@]} + ${#FAILED_CASES[@]} ))
FAILED_CASE_COUNT=${#FAILED_CASES[@]}
PASS_PERCENT=0
if [ "$TOTAL_CASE_COUNT" -gt 0 ]; then
  PASS_PERCENT=$(( (${#PASSED_CASES[@]} * 100) / TOTAL_CASE_COUNT ))
fi

write_detailed_log "${#PASSED_CASES[@]}" "$FAILED_CASE_COUNT" "$TOTAL_CASE_COUNT" "$PASS_PERCENT"
build_windows_codex_exe

echo "DISCOVERY: codex_binary=$CODEX_BINARY case_lines_seen=$TOTAL_CASE_COUNT"
echo "INFO: PeanutButterCodexTests is built with PEANUT_BUTTER_ULTIMA_TEST_BUILD (BLOCK_SIZE_L3=24u)."
echo "RANDOM CASES: seen=${RANDOM_CASE_SEEN_COUNT}/${RANDOM_CASE_SOURCE_COUNT}"
if [ -n "$WINDOWS_EXE_PATH" ]; then
  echo "WINDOWS EXE: $WINDOWS_EXE_PATH ($WINDOWS_BUILD_NOTE)"
else
  echo "WINDOWS EXE: $WINDOWS_BUILD_NOTE"
fi

if [ "${#MISSING_RANDOM_CASES[@]}" -gt 0 ]; then
  echo "RESULT: FAIL"
  echo "Random tests not linked into codex runner (${#MISSING_RANDOM_CASES[@]} missing of ${RANDOM_CASE_SOURCE_COUNT} expected)."
  echo "Detailed log: $DETAILED_LOG_FILE"
  exit 8
fi

if [ "$CODEX_EXIT_CODE" -eq 0 ] && [ "$FAILED_CASE_COUNT" -eq 0 ]; then
  echo "RESULT: PASS"
  echo "${PASS_PERCENT}% tests passed, ${FAILED_CASE_COUNT} tests failed out of ${TOTAL_CASE_COUNT} case-level tests"
  echo "Detailed log: $DETAILED_LOG_FILE"
  exit 0
fi

echo "RESULT: FAIL"
echo "Codex test binary exit code: $CODEX_EXIT_CODE"
echo "${PASS_PERCENT}% tests passed, ${FAILED_CASE_COUNT} tests failed out of ${TOTAL_CASE_COUNT} case-level tests"
echo "Detailed log: $DETAILED_LOG_FILE"
if [ "$FAILED_CASE_COUNT" -gt 0 ]; then
  echo "FAILED CASES:"
  for case_name in "${FAILED_CASES[@]}"; do
    echo "  - $case_name"
  done
fi
exit 8
