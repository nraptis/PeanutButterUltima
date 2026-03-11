#!/usr/bin/env python3
"""
Triage generated fence test failures by combining:
  - run_brew_tests_detailed_logs.txt
  - tests/generated/test_cases_all.json

Usage:
  python3 triage_generated_failures.py
  python3 triage_generated_failures.py --only AUTO_FileContent_InArchiveHeader_SMALL_075
"""

from __future__ import annotations

import argparse
import ast
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


DEFAULT_LOG_PATH = "run_brew_tests_detailed_logs.txt"
DEFAULT_CASES_PATH = "tests/generated/test_cases_all.json"
DEFAULT_FORMAT_CONSTANTS_PATH = "src/FormatConstants.hpp"
SCRIPT_DIR = Path(__file__).resolve().parent


@dataclass
class FailureRecord:
    case_id: str
    reason: str
    bucket: str
    surfaced_error: str
    flow: str = ""
    field_kind: str = ""
    illegal_target_type: str = ""
    expected_error_code: str = ""
    archive_payload_length: int = 0
    mutation_archive_index: int = -1
    mutation_offset: int = -1
    mutation_width: int = -1
    selected_subject: str = ""


FAIL_LINE_RE = re.compile(r"^\[FAIL\]\s+(AUTO_[A-Za-z0-9_]+)\s+failed:\s+(.*)$")


def classify_bucket(reason: str) -> str:
    if (
        "mutation stage failed: archive byte mutation exceeded archive bounds" in reason
        or "mutation stage failed: archive data mutation exceeded archive bounds" in reason
    ):
        return "mutation_out_of_bounds"
    if "mutated unbundle unexpectedly succeeded" in reason:
        return "unexpected_success"
    if "did not surface expected error code" in reason:
        return "expected_code_mismatch"
    if "missing expected error code and generic failure signal" in reason:
        return "missing_expected_and_no_generic_failure"
    if "mutated bundle stage failed" in reason:
        return "mutated_bundle_stage_failure"
    if "healthy bundle failed" in reason or "healthy unbundle failed" in reason:
        return "healthy_smoke_failure"
    return "other"


def load_case_index(path: Path) -> Dict[str, dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    cases = data.get("cases", [])
    return {str(c.get("case_id", "")): c for c in cases if c.get("case_id")}


def load_cases_and_config(path: Path) -> tuple[Dict[str, dict], Dict[str, object]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    cases = data.get("cases", [])
    case_index = {str(c.get("case_id", "")): c for c in cases if c.get("case_id")}
    config = dict(data.get("config", {}))
    return case_index, config


def _strip_cpp_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    source = re.sub(r"//.*", "", source)
    return source


def _eval_int_expr(expr: str, symbols: Dict[str, int]) -> int:
    node = ast.parse(expr, mode="eval")

    def visit(n: ast.AST) -> int:
        if isinstance(n, ast.Expression):
            return visit(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return int(n.value)
        if isinstance(n, ast.Name):
            if n.id not in symbols:
                raise RuntimeError(f"Unknown symbol '{n.id}' in constexpr expression '{expr}'.")
            return int(symbols[n.id])
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.UAdd, ast.USub)):
            value = visit(n.operand)
            return value if isinstance(n.op, ast.UAdd) else -value
        if isinstance(n, ast.BinOp):
            left = visit(n.left)
            right = visit(n.right)
            if isinstance(n.op, ast.Add):
                return left + right
            if isinstance(n.op, ast.Sub):
                return left - right
            if isinstance(n.op, ast.Mult):
                return left * right
            if isinstance(n.op, (ast.Div, ast.FloorDiv)):
                return left // right
            if isinstance(n.op, ast.Mod):
                return left % right
            if isinstance(n.op, ast.LShift):
                return left << right
            if isinstance(n.op, ast.RShift):
                return left >> right
            if isinstance(n.op, ast.BitOr):
                return left | right
            if isinstance(n.op, ast.BitAnd):
                return left & right
            if isinstance(n.op, ast.BitXor):
                return left ^ right
        raise RuntimeError(f"Unsupported constexpr expression '{expr}'.")

    return int(visit(node))


def _extract_test_build_block(source: str) -> str:
    match = re.search(
        r"#ifdef\s+PEANUT_BUTTER_ULTIMA_TEST_BUILD(.*?)#else",
        source,
        flags=re.DOTALL,
    )
    if not match:
        raise RuntimeError("Could not find #ifdef PEANUT_BUTTER_ULTIMA_TEST_BUILD block.")
    return match.group(1)


def parse_test_execution_l1_payload_length(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    clean = _strip_cpp_comments(text)
    assignments = re.findall(
        r"inline\s+constexpr\s+std::size_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+)\s*;",
        clean,
    )
    symbols: Dict[str, int] = {}
    for name, expr in assignments:
        symbols[name] = _eval_int_expr(expr.strip(), symbols)

    test_block = _strip_cpp_comments(_extract_test_build_block(text))
    payload_match = re.search(r"SB_PAYLOAD_SIZE\s*=\s*([^;]+)\s*;", test_block)
    if not payload_match:
        raise RuntimeError(
            "Could not parse SB_PAYLOAD_SIZE from active test-build branch in FormatConstants.hpp."
        )
    return _eval_int_expr(payload_match.group(1).strip(), symbols)


def print_l1_geometry_summary(generation_l1_payload: Optional[int], test_execution_l1_payload: Optional[int]) -> None:
    print("L1 payload geometry:")
    gen_text = str(generation_l1_payload) if generation_l1_payload is not None else "unknown"
    run_text = str(test_execution_l1_payload) if test_execution_l1_payload is not None else "unknown"
    print(f"  Generation Data Per L1:      {gen_text}")
    print(f"  Test Execution Data Per L1:  {run_text}")
    if generation_l1_payload is not None and test_execution_l1_payload is not None:
        if generation_l1_payload == test_execution_l1_payload:
            print("  Geometry Status:             MATCH")
        else:
            print("  Geometry Status:             MISMATCH")
    print()


def extract_failures(log_lines: List[str]) -> List[FailureRecord]:
    failures: List[FailureRecord] = []
    for i, line in enumerate(log_lines):
        match = FAIL_LINE_RE.match(line.strip())
        if not match:
            continue
        case_id = match.group(1)
        reason = match.group(2).strip()
        bucket = classify_bucket(reason)

        surfaced_error = ""
        j = i + 1
        while j < len(log_lines) and not log_lines[j].startswith("0% tests passed"):
            cur = log_lines[j].strip()
            if not surfaced_error and (
                "Unbundle failed:" in cur or "Recover failed:" in cur or "mutation stage failed:" in cur
            ):
                surfaced_error = cur
            j += 1

        failures.append(
            FailureRecord(
                case_id=case_id,
                reason=reason,
                bucket=bucket,
                surfaced_error=surfaced_error,
            )
        )
    return failures


def enrich_with_case_data(failures: Iterable[FailureRecord], case_index: Dict[str, dict]) -> None:
    for rec in failures:
        case = case_index.get(rec.case_id)
        if not case:
            continue
        rec.flow = str(case.get("flow", ""))
        rec.field_kind = str(case.get("field_kind", ""))
        rec.illegal_target_type = str(case.get("illegal_target_type", ""))
        rec.expected_error_code = str(case.get("expected_error_code", ""))
        rec.archive_payload_length = int(case.get("archive_payload_length", 0))
        rec.selected_subject = str(case.get("selected_subject", ""))
        mut = case.get("mutation", {}) or {}
        rec.mutation_archive_index = int(mut.get("archive_index", -1))
        rec.mutation_offset = int(mut.get("offset", -1))
        rec.mutation_width = int(mut.get("width_bytes", -1))


def print_summary(failures: List[FailureRecord], only_ids: Optional[set[str]]) -> None:
    if only_ids:
        failures = [f for f in failures if f.case_id in only_ids]

    print(f"Total failed cases parsed: {len(failures)}")
    if not failures:
        return

    bucket_counter = Counter(f.bucket for f in failures)
    print("\nBy bucket:")
    for bucket, count in bucket_counter.most_common():
        print(f"  - {bucket}: {count}")

    by_kind_target: Dict[str, Counter] = defaultdict(Counter)
    for f in failures:
        key = f"{f.field_kind} -> {f.illegal_target_type}"
        by_kind_target[f.bucket][key] += 1

    print("\nBucket details (field_kind -> target):")
    for bucket, counts in by_kind_target.items():
        print(f"  {bucket}:")
        for key, count in counts.most_common():
            print(f"    - {key}: {count}")

    print("\nCase details:")
    for f in failures:
        print(f"- {f.case_id}")
        print(f"  bucket={f.bucket}")
        print(f"  flow={f.flow} field_kind={f.field_kind} target={f.illegal_target_type}")
        print(f"  expected={f.expected_error_code} surfaced={f.surfaced_error or '(none)'}")
        print(
            f"  mutation=archive[{f.mutation_archive_index}] off={f.mutation_offset} width={f.mutation_width} "
            f"payload_len={f.archive_payload_length}"
        )
        if f.reason:
            print(f"  reason={f.reason}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Triage generated fence test failures.")
    parser.add_argument("--log", default=DEFAULT_LOG_PATH, help="Path to run_brew_tests_detailed_logs.txt")
    parser.add_argument("--cases", default=DEFAULT_CASES_PATH, help="Path to test_cases_all.json")
    parser.add_argument(
        "--format-constants",
        default=DEFAULT_FORMAT_CONSTANTS_PATH,
        help="Path to src/FormatConstants.hpp used by current test execution build.",
    )
    parser.add_argument("--only", nargs="*", default=[], help="Optional list of AUTO_ case IDs to filter")
    return parser.parse_args()


def resolve_input_path(raw_path: str, label: str) -> Path:
    candidate = Path(raw_path)
    if candidate.is_absolute():
        if candidate.exists():
            return candidate
        raise SystemExit(f"Missing {label} file: {candidate}")

    cwd_candidate = Path.cwd() / candidate
    if cwd_candidate.exists():
        return cwd_candidate

    script_candidate = SCRIPT_DIR / candidate
    if script_candidate.exists():
        return script_candidate

    raise SystemExit(
        f"Missing {label} file: tried '{cwd_candidate}' and '{script_candidate}'.\n"
        f"Tip: run './run_brew_tests.sh' from the repo root first."
    )


def main() -> None:
    args = parse_args()
    log_path = resolve_input_path(args.log, "log")
    cases_path = resolve_input_path(args.cases, "cases")
    format_constants_path = resolve_input_path(args.format_constants, "format constants")

    log_lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    failures = extract_failures(log_lines)
    case_index, config = load_cases_and_config(cases_path)
    generation_l1_payload = None
    raw_generation_l1_payload = config.get("l1_payload_length")
    if raw_generation_l1_payload is not None:
        try:
            generation_l1_payload = int(raw_generation_l1_payload)
        except Exception:
            generation_l1_payload = None
    test_execution_l1_payload = None
    try:
        test_execution_l1_payload = parse_test_execution_l1_payload_length(format_constants_path)
    except Exception as exc:
        print(f"[WARN] could not parse test execution payload geometry: {exc}")

    print_l1_geometry_summary(generation_l1_payload, test_execution_l1_payload)
    enrich_with_case_data(failures, case_index)

    only_ids = set(args.only) if args.only else None
    print_summary(failures, only_ids)


if __name__ == "__main__":
    main()
