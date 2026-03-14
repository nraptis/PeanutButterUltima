#!/usr/bin/env zsh

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-debug"
TEST_OUTPUT_FILE="$BUILD_DIR/codex_cases_output.txt"
DETAILED_LOG_FILE="$SCRIPT_DIR/run_brew_tests_detailed_logs.txt"

BUILD_DIRS=(
  "$SCRIPT_DIR/build"
  "$SCRIPT_DIR/build-debug"
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

for BUILD_PATH in "${BUILD_DIRS[@]}"; do
  if [ -d "$BUILD_PATH" ]; then
    rm -rf "$BUILD_PATH"
  fi
done

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPEANUT_BUTTER_ULTIMA_BUILD_APP=OFF \
  -DPEANUT_BUTTER_ULTIMA_BUILD_TESTS=ON
BUILD_EXIT_CODE=$?
if [ "$BUILD_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: BUILD CONFIGURE FAILED"
  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "Build configure failed with exit code $BUILD_EXIT_CODE."
  } > "$DETAILED_LOG_FILE"
  exit "$BUILD_EXIT_CODE"
fi

cmake --build "$BUILD_DIR" --config Debug -j4
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

RANDOM_CASE_SOURCE_COUNT=0
if [ -d "$SCRIPT_DIR/tests/random" ]; then
  RANDOM_CASE_SOURCE_COUNT=$(find "$SCRIPT_DIR/tests/random" -maxdepth 1 -type f -name 'Test*.cpp' | wc -l | tr -d '[:space:]')
  for random_src in "$SCRIPT_DIR"/tests/random/Test*.cpp(N); do
    random_case_name="$(basename "$random_src" .cpp)"
    if [ -z "${SEEN_CASES[$random_case_name]-}" ]; then
      MISSING_RANDOM_CASES+=("$random_case_name")
    else
      RANDOM_CASE_SEEN_COUNT=$((RANDOM_CASE_SEEN_COUNT + 1))
    fi
  done
fi

TOTAL_CASE_COUNT=$(( ${#PASSED_CASES[@]} + ${#FAILED_CASES[@]} ))
FAILED_CASE_COUNT=${#FAILED_CASES[@]}
PASS_PERCENT=0
if [ "$TOTAL_CASE_COUNT" -gt 0 ]; then
  PASS_PERCENT=$(( (${#PASSED_CASES[@]} * 100) / TOTAL_CASE_COUNT ))
fi

write_detailed_log "${#PASSED_CASES[@]}" "$FAILED_CASE_COUNT" "$TOTAL_CASE_COUNT" "$PASS_PERCENT"

echo "DISCOVERY: codex_binary=$CODEX_BINARY case_lines_seen=$TOTAL_CASE_COUNT"
echo "INFO: PeanutButterCodexTests is built with PEANUT_BUTTER_ULTIMA_TEST_BUILD."
echo "RANDOM CASES: seen=${RANDOM_CASE_SEEN_COUNT}/${RANDOM_CASE_SOURCE_COUNT}"

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
