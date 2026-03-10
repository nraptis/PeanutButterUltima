#!/usr/bin/env python3
"""
Examples:
  python3 test_case_gen_executor_readable.py
"""
from __future__ import annotations

import json
from typing import Any, Dict, List


INPUT_JSON = "tests/generated/test_cases.json"
LIMIT = 0  # 0 means show all.


def describe_case(case: Dict[str, Any]) -> str:
    m = case["mutation"]
    tree = case["tree"]
    return (
        f"{case['case_id']} [{case['size_class']}/{case['flow']}] "
        f"payload={case.get('archive_payload_length')} l1={case.get('l1_length')} "
        f"{case['field_kind']} -> {case['illegal_target_type']}\n"
        f"  expect: {case.get('expected_error_code', 'UNK_SYS_001')}\n"
        f"  files={len(tree.get('files', []))}, empty_dirs={len(tree.get('empty_dirs', []))}\n"
        f"  subject: {case['selected_subject']}\n"
        f"  failure point: {case.get('failure_point_comment', '')}\n"
        f"  mutate: archive #{m['archive_index']}, byte #{m['offset']}, "
        f"write {m['width_bytes']} bytes, value={m['new_value']} (LE hex={m['new_value_le_hex']})"
    )


def main() -> None:
    with open(INPUT_JSON, "r", encoding="utf-8") as f:
        data = json.load(f)

    cases: List[Dict[str, Any]] = data.get("cases", [])
    if LIMIT > 0:
        cases = cases[:LIMIT]

    print("Generated Test Cases")
    print(f"  version: {data.get('version')}")
    print(f"  seed: {data.get('seed')}")
    print(f"  total: {len(data.get('cases', []))}")
    print("")

    for case in cases:
        print(describe_case(case))
        notes = case.get("notes", [])
        if notes:
            for n in notes:
                print(f"  note: {n}")
        print("")


if __name__ == "__main__":
    main()
