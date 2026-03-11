#!/usr/bin/env zsh

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-test-release"
TEST_LIST_FILE="$BUILD_DIR/ctest_brew_list.txt"
TEST_OUTPUT_FILE="$BUILD_DIR/ctest_brew_output.txt"
DETAILED_LOG_FILE="$SCRIPT_DIR/run_brew_tests_detailed_logs.txt"

BUILD_DIRS=(
  "$SCRIPT_DIR/build"
  "$SCRIPT_DIR/build-debug"
  "$SCRIPT_DIR/build-test-release"
  "$SCRIPT_DIR/build-release"
)

typeset -a TEST_NAMES
typeset -a PASSED_TESTS
typeset -a FAILED_TESTS
typeset -A FAILED_REASONS

{
  echo "run_brew_tests_detailed_logs.txt"
  echo "generated_at: $(date '+%Y-%m-%d %H:%M:%S %Z')"
  echo "status: run started"
  echo ""
} > "$DETAILED_LOG_FILE"

sanitize_stem() {
  echo "$1" | sed -E 's/[^A-Za-z0-9_]/_/g'
}

label_for_test() {
  local test_name="$1"
  local suffix src stem sanitized
  case "$test_name" in
    PeanutButterUltimaQuickSmoke)
      if [ -f "$SCRIPT_DIR/quick_tests/QuickSmoke.cpp" ]; then
        echo "QuickSmoke.cpp"
        return
      fi
      ;;
    PeanutButterUltimaCodex_*)
      suffix="${test_name#PeanutButterUltimaCodex_}"
      for src in "$SCRIPT_DIR"/tests/codex/*.cpp(N); do
        stem="$(basename "$src" .cpp)"
        sanitized="$(sanitize_stem "$stem")"
        if [ "$sanitized" = "$suffix" ]; then
          basename "$src"
          return
        fi
      done
      ;;
    PeanutButterUltimaGenerated_*)
      suffix="${test_name#PeanutButterUltimaGenerated_}"
      for src in "$SCRIPT_DIR"/tests/generated/*.cpp(N); do
        stem="$(basename "$src" .cpp)"
        sanitized="$(sanitize_stem "$stem")"
        if [ "$sanitized" = "$suffix" ]; then
          basename "$src"
          return
        fi
      done
      ;;
  esac
  echo "$test_name"
}

write_detailed_log() {
  local passed_count="$1"
  local failed_count="$2"
  local total_count="$3"
  local pass_percent="$4"
  local test_name label log_file reason

  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "generated_at: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "summary: ${passed_count} passed, ${failed_count} failed, ${total_count} total (${pass_percent}% passed)"
    echo ""

    if [ "$failed_count" -gt 0 ]; then
      for test_name in "${FAILED_TESTS[@]}"; do
        label="$(label_for_test "$test_name")"
        echo "${label} FAILED"
      done
    else
      echo "No failed tests."
    fi

    echo ""

    for test_name in "${FAILED_TESTS[@]}"; do
      label="$(label_for_test "$test_name")"
      log_file="$BUILD_DIR/ctest_$(sanitize_stem "$test_name").txt"
      reason="${FAILED_REASONS[$test_name]-}"
      if [ -z "$reason" ]; then
        reason="No parsed failure summary."
      fi

      echo "${label} FAILED, Summary"
      echo "$reason"
      echo ""
      echo "${label} FAILED, Logs"
      if [ -n "$log_file" ] && [ -f "$log_file" ]; then
        cat "$log_file"
      else
        echo "No log output captured for $test_name."
      fi
      echo "----"
      echo ""
    done

    if [ "$passed_count" -gt 0 ]; then
      for test_name in "${PASSED_TESTS[@]}"; do
        label="$(label_for_test "$test_name")"
        echo "${label} PASSED!"
      done
    else
      echo "No passed tests."
    fi
  } > "$DETAILED_LOG_FILE"
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

ctest --test-dir "$BUILD_DIR" -N > "$TEST_LIST_FILE"
CTEST_LIST_EXIT_CODE=$?
if [ "$CTEST_LIST_EXIT_CODE" -ne 0 ]; then
  echo "RESULT: FAIL"
  echo "CTest discovery failed (ctest -N exit code $CTEST_LIST_EXIT_CODE)."
  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "CTest discovery failed (ctest -N exit code $CTEST_LIST_EXIT_CODE)."
    if [ -f "$TEST_LIST_FILE" ]; then
      echo ""
      echo "CTEST DISCOVERY OUTPUT:"
      cat "$TEST_LIST_FILE"
    fi
  } > "$DETAILED_LOG_FILE"
  if [ -f "$TEST_LIST_FILE" ]; then
    echo "CTEST DISCOVERY OUTPUT:"
    cat "$TEST_LIST_FILE"
  fi
  exit "$CTEST_LIST_EXIT_CODE"
fi

while IFS= read -r line; do
  test_name="$(echo "$line" | sed -nE 's/^[[:space:]]*Test[[:space:]]*#[0-9]+:[[:space:]]*(.+)$/\1/p')"
  if [ -n "$test_name" ]; then
    TEST_NAMES+=("$test_name")
  fi
done < "$TEST_LIST_FILE"

typeset -a GENERATED_TEST_NAMES
for test_name in "${TEST_NAMES[@]}"; do
  case "$test_name" in
    PeanutButterUltimaGenerated_*)
      GENERATED_TEST_NAMES+=("$test_name")
      ;;
  esac
done
TEST_NAMES=("${GENERATED_TEST_NAMES[@]}")

GENERATED_SOURCE_COUNT=$(find "$SCRIPT_DIR/tests/generated" -maxdepth 1 -type f -name '*.cpp' | wc -l | tr -d '[:space:]')

GENERATED_TEST_COUNT=0
for test_name in "${TEST_NAMES[@]}"; do
  case "$test_name" in
    PeanutButterUltimaGenerated_*)
      GENERATED_TEST_COUNT=$((GENERATED_TEST_COUNT + 1))
      ;;
  esac
done

if [ "$GENERATED_SOURCE_COUNT" -gt 0 ] && [ "$GENERATED_TEST_COUNT" -eq 0 ]; then
  echo "RESULT: FAIL"
  echo "Generated tests exist in tests/generated but no generated tests were registered by CMake."
  echo "CTEST DISCOVERY OUTPUT:"
  sed -n '1,200p' "$TEST_LIST_FILE"
  {
    echo "run_brew_tests_detailed_logs.txt"
    echo "Generated tests exist in tests/generated but no generated tests were registered by CMake."
    echo ""
    echo "CTEST DISCOVERY OUTPUT:"
    sed -n '1,200p' "$TEST_LIST_FILE"
  } > "$DETAILED_LOG_FILE"
  exit 1
fi

echo "DISCOVERY: generated_sources=$GENERATED_SOURCE_COUNT generated_tests=$GENERATED_TEST_COUNT"

: > "$TEST_OUTPUT_FILE"

for test_name in "${TEST_NAMES[@]}"; do
  safe_name="$(sanitize_stem "$test_name")"
  single_output_file="$BUILD_DIR/ctest_${safe_name}.txt"
  fail_reason=""

  ctest --test-dir "$BUILD_DIR" --output-on-failure -R "^${test_name}$" > "$single_output_file" 2>&1
  single_exit_code=$?

  cat "$single_output_file" >> "$TEST_OUTPUT_FILE"
  echo "" >> "$TEST_OUTPUT_FILE"

  if [ "$single_exit_code" -eq 0 ]; then
    PASSED_TESTS+=("$test_name")
  else
    fail_reason="$(sed -nE 's/^\[FAIL\][[:space:]]*(.*)$/\1/p' "$single_output_file" | head -n 1)"
    if [ -z "$fail_reason" ]; then
      fail_reason="$(sed -nE '/(failed|FAILED|Expected|expected|got)/Ip' "$single_output_file" | head -n 1)"
    fi
    if [ -z "$fail_reason" ]; then
      fail_reason="Non-zero test exit with no parsed [FAIL] marker."
    fi
    FAILED_REASONS[$test_name]="$fail_reason"
    FAILED_TESTS+=("$test_name")
  fi
done

TOTAL_TEST_COUNT=${#TEST_NAMES[@]}
PASSED_TEST_COUNT=${#PASSED_TESTS[@]}
FAILED_TEST_COUNT=${#FAILED_TESTS[@]}
PASS_PERCENT=0
if [ "$TOTAL_TEST_COUNT" -gt 0 ]; then
  PASS_PERCENT=$(( (PASSED_TEST_COUNT * 100) / TOTAL_TEST_COUNT ))
fi

write_detailed_log "$PASSED_TEST_COUNT" "$FAILED_TEST_COUNT" "$TOTAL_TEST_COUNT" "$PASS_PERCENT"

if [ "$FAILED_TEST_COUNT" -eq 0 ]; then
  echo "RESULT: PASS"
  echo "${PASS_PERCENT}% tests passed, ${FAILED_TEST_COUNT} tests failed out of ${TOTAL_TEST_COUNT}"
  echo "Detailed log: $DETAILED_LOG_FILE"
  exit 0
fi

echo "RESULT: FAIL"
echo "${PASS_PERCENT}% tests passed, ${FAILED_TEST_COUNT} tests failed out of ${TOTAL_TEST_COUNT}"
echo "Detailed log: $DETAILED_LOG_FILE"
if [ "$FAILED_TEST_COUNT" -gt 0 ]; then
  echo "FAILED TESTS:"
  for test_name in "${FAILED_TESTS[@]}"; do
    echo "  - $test_name"
  done
fi
exit 8
