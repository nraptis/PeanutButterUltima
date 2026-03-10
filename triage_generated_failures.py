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
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


DEFAULT_LOG_PATH = "run_brew_tests_detailed_logs.txt"
DEFAULT_CASES_PATH = "tests/generated/test_cases_all.json"
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
    if "mutation stage failed: archive byte mutation exceeded archive bounds" in reason:
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

    log_lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    failures = extract_failures(log_lines)
    case_index = load_case_index(cases_path)
    enrich_with_case_data(failures, case_index)

    only_ids = set(args.only) if args.only else None
    print_summary(failures, only_ids)


if __name__ == "__main__":
    main()
