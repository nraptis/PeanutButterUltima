#!/usr/bin/env python3
"""
Generate all planned fence mutation cases in one pass.

This script intentionally does NOT iterate a flow matrix. Flow is derived from
source/target semantics:
  - recovery_header => recover
  - make_zero => recover
  - everything else => unbundle

Outputs:
  - tests/generated/test_cases_all.json
  - tests/generated/test_cases_all_index.txt
"""

from __future__ import annotations

import ast
import json
import random
import re
import subprocess
import sys
import hashlib
from dataclasses import asdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import test_case_gen_engine as engine

SCRIPT_DIR = Path(__file__).resolve().parent

# Narrow/specialized fence scope: one deterministic case per family/target.
SMALL_CASES_PER_SPEC = 32
MEDIUM_CASES_PER_SPEC = 16
ENCODE_TESTS_CASES_AS_DATA = True

SOURCE_TYPES = [
    "file_name_length",
    "file_content_length",
    "directory_name_length",
    "recovery_header",
    "eof_gar",
    "eof_oob",
]

TARGET_TYPES = [
    "out_of_entire_archive_list_bounds",
    "out_of_this_archive_bounds",
    "within_recovery_header",
    "within_archive_header",
    "make_zero",
]


MAX_CASE_RETRIES = 64
RANDOM_SEED = 20260310

FORMAT_CONSTANTS_PATH = SCRIPT_DIR / "src" / "FormatConstants.hpp"
OUTPUT_DIR = SCRIPT_DIR / "tests" / "generated"
OUTPUT_JSON = OUTPUT_DIR / "test_cases_all.json"
OUTPUT_INDEX = OUTPUT_DIR / "test_cases_all_index.txt"
ENGINE_OUTPUT_JSON = OUTPUT_DIR / "test_cases.json"



def run_script_command(command: List[str], step_name: str) -> None:
    print(f"[{step_name}] {' '.join(command)}", flush=True)
    subprocess.run(command, check=True, cwd=SCRIPT_DIR)


def run_engine_step() -> None:
    # Bootstrap output for test_cases.json; this is intentionally separate from
    # the all-spec generation loop below, but uses the same per-spec counts.
    command = [
        sys.executable,
        str(SCRIPT_DIR / "test_case_gen_engine.py"),
        "--failure-source-type",
        "any",
        "--failure-target-type",
        "any",
        "--flow-type",
        "any",
        "--include-eof-source-types-in-any",
        "--small-count",
        str(SMALL_CASES_PER_SPEC),
        "--medium-count",
        str(MEDIUM_CASES_PER_SPEC),
        "--random-seed",
        str(RANDOM_SEED),
        "--output-json",
        str(ENGINE_OUTPUT_JSON),
        "--format-constants-path",
        str(FORMAT_CONSTANTS_PATH),
        "--sync-format-constants",
    ]
    run_script_command(command, "engine")


def run_executor_cpp_step() -> None:
    executor_name = "test_case_gen_executor_cpp_data.py" if ENCODE_TESTS_CASES_AS_DATA else "test_case_gen_executor_cpp_code.py"
    command = [
        sys.executable,
        str(SCRIPT_DIR / executor_name),
        "--input",
        str(OUTPUT_JSON),
        "--output-dir",
        str(OUTPUT_DIR),
    ]
    run_script_command(command, "executor_cpp")


def parse_format_constants(p_path: Path) -> Dict[str, int]:
    text = p_path.read_text(encoding="utf-8")

    def strip_cpp_comments(source: str) -> str:
        source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
        source = re.sub(r"//.*", "", source)
        return source

    def eval_int_expr(expr: str, symbols: Dict[str, int]) -> int:
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

    def extract_test_build_block(source: str) -> str:
        match = re.search(
            r"#ifdef\s+PEANUT_BUTTER_ULTIMA_TEST_BUILD(.*?)#else",
            source,
            flags=re.DOTALL,
        )
        if not match:
            raise RuntimeError("Could not find PEANUT_BUTTER_ULTIMA_TEST_BUILD block in FormatConstants.hpp.")
        return match.group(1)

    def parse_named(name: str) -> int:
        match = re.search(rf"{name}\s*=\s*([0-9]+)\s*;", text)
        if not match:
            raise RuntimeError(f"Could not parse {name} in {p_path}")
        return int(match.group(1))

    plain_header_len = parse_named("SB_PLAIN_TEXT_HEADER_LENGTH")
    checksum_len = parse_named("SB_RECOVERY_CHECKSUM_LENGTH")
    stride_len = parse_named("SB_RECOVERY_STRIDE_LENGTH")

    symbols = {
        "SB_PLAIN_TEXT_HEADER_LENGTH": plain_header_len,
        "SB_RECOVERY_CHECKSUM_LENGTH": checksum_len,
        "SB_RECOVERY_STRIDE_LENGTH": stride_len,
        "SB_RECOVERY_HEADER_LENGTH": checksum_len + stride_len,
        "EB_MAX_LENGTH": parse_named("EB_MAX_LENGTH"),
    }

    test_block = strip_cpp_comments(extract_test_build_block(text))
    payload_match = re.search(r"SB_PAYLOAD_SIZE\s*=\s*([^;]+)\s*;", test_block)
    if not payload_match:
        raise RuntimeError(
            "Could not parse test-build SB_PAYLOAD_SIZE from FormatConstants.hpp "
            "(expected PEANUT_BUTTER_ULTIMA_TEST_BUILD branch)."
        )

    payload_len = eval_int_expr(payload_match.group(1).strip(), symbols)
    return {
        "plain_header_length": plain_header_len,
        "recovery_checksum_length": checksum_len,
        "recovery_stride_length": stride_len,
        "recovery_header_length": checksum_len + stride_len,
        "l1_payload_length": payload_len,
    }


def apply_format_constants_to_engine(p_constants: Dict[str, int]) -> None:
    engine.PLAIN_HEADER_LENGTH = p_constants["plain_header_length"]
    engine.RECOVERY_HEADER_LENGTH = p_constants["recovery_header_length"]
    engine.L1_PAYLOAD_LENGTH = p_constants["l1_payload_length"]
    engine.L1_LENGTH = engine.L1_PAYLOAD_LENGTH + engine.RECOVERY_HEADER_LENGTH
    engine.SB_L3_LENGTH_DEFAULT = engine.L1_LENGTH * 4
    engine.RECOVERY_DIST_WIDTH = p_constants["recovery_stride_length"]


def source_stem(p_source: str) -> str:
    mapping = {
        "file_name_length": "FileName",
        "file_content_length": "FileContent",
        "directory_name_length": "FolderName",
        "recovery_header": "RecoveryStride",
        "eof_gar": "EndOfFile",
        "eof_oob": "EndOfFile",
    }
    if p_source not in mapping:
        raise ValueError(f"Unsupported source type: {p_source}")
    return mapping[p_source]


def pretty_case_name(p_source: str, p_target: Optional[str]) -> str:
    if p_source == "eof_gar":
        return "EndOfFile_TrailingGarbage"
    if p_source == "eof_oob":
        return "EndOfFile_TrailingArchives"

    if p_target == "make_zero":
        if p_source == "file_name_length":
            return "FileName_Zero_MidPayload"
        if p_source == "directory_name_length":
            return "FolderName_Zero_MidPayload"
        if p_source == "file_content_length":
            return "FileContent_Zero_Desync"
        if p_source == "recovery_header":
            return "RecoveryStride_Zero"

    suffix = {
        "out_of_entire_archive_list_bounds": "MissingArchive",
        "out_of_this_archive_bounds": "Outlying",
        "within_recovery_header": "InRecoveryHeader",
        "within_archive_header": "InArchiveHeader",
    }.get(p_target)

    if not suffix:
        raise ValueError(f"Unsupported target type for pretty name: {p_target}")

    return f"{source_stem(p_source)}_{suffix}"


def pretty_cpp_file_name(p_source: str, p_target: Optional[str]) -> str:
    return f"Test_Fences_{pretty_case_name(p_source, p_target)}.cpp"


def preferred_flow(p_source: str, p_target: Optional[str]) -> str:
    if p_source == "recovery_header":
        return "recover"
    if p_target == "make_zero":
        return "recover"
    return "unbundle"


def override_expected_error_for_make_zero(p_source: str, p_existing: str) -> str:
    if p_source in ("file_name_length", "directory_name_length", "file_content_length"):
        return "UNP_EOF_003"
    if p_source == "recovery_header":
        return "UNP_EOF_003"
    return p_existing


def expected_fence_flag_for_target(p_source: str, p_target: Optional[str]) -> str:
    if p_source in ("eof_gar", "eof_oob"):
        return ""
    mapping = {
        "within_archive_header": "FENCE_IN_ARCHIVE_HEADER",
        "within_recovery_header": "FENCE_IN_RECOVERY_HEADER",
        "out_of_entire_archive_list_bounds": "FENCE_IN_GAP_ARCHIVE",
        "out_of_this_archive_bounds": "FENCE_OUTSIDE_PAYLOAD_RANGE",
    }
    return mapping.get(p_target, "")


def forbidden_fence_flags_for_target(p_source: str, p_target: Optional[str]) -> List[str]:
    if p_source not in ("file_name_length", "file_content_length", "directory_name_length"):
        return []
    if p_target == "within_archive_header":
        return ["FENCE_IN_RECOVERY_HEADER"]
    if p_target == "within_recovery_header":
        return ["FENCE_IN_ARCHIVE_HEADER"]
    return []


def forced_generation_target(p_target: Optional[str]) -> Optional[str]:
    if p_target in engine.TARGET_TYPES:
        return p_target
    if p_target == "make_zero":
        # Generate a valid mutation anchor first, then force mutation value to zero.
        return "out_of_this_archive_bounds"
    return None


def archive_block_count_for_case(p_case: engine.TestCase) -> int:
    if engine.SB_L3_LENGTH_DEFAULT <= 0:
        raise RuntimeError("SB_L3_LENGTH_DEFAULT must be positive.")
    if (p_case.archive_payload_length % engine.SB_L3_LENGTH_DEFAULT) != 0:
        raise RuntimeError(
            f"Case {p_case.case_id} payload is not an even SB_L3 multiple: "
            f"{p_case.archive_payload_length} vs {engine.SB_L3_LENGTH_DEFAULT}."
        )
    block_count = p_case.archive_payload_length // engine.SB_L3_LENGTH_DEFAULT
    if block_count not in engine.ALLOWED_ARCHIVE_BLOCK_COUNTS:
        raise RuntimeError(
            f"Case {p_case.case_id} produced invalid archive block count {block_count}; "
            f"allowed={engine.ALLOWED_ARCHIVE_BLOCK_COUNTS}."
        )
    return block_count


def to_case_dict(p_case: engine.TestCase,
                 p_source: str,
                 p_target: Optional[str],
                 p_requested_target: Optional[str]) -> Dict[str, object]:
    archive_block_count = archive_block_count_for_case(p_case)
    expected_fence_flag = expected_fence_flag_for_target(p_source, p_target)
    forbidden_fence_flags = forbidden_fence_flags_for_target(p_source, p_target)
    return {
        "case_id": p_case.case_id,
        "size_class": p_case.size_class,
        "flow": p_case.flow,
        "archive_payload_length": p_case.archive_payload_length,
        "archive_block_count": archive_block_count,
        "l1_length": p_case.l1_length,
        "field_kind": p_case.field_kind,
        "illegal_target_type": p_case.illegal_target_type,
        "target_type_requested": p_requested_target,
        "expected_error_code": p_case.expected_error_code,
        "expected_fence_flag": expected_fence_flag,
        "forbidden_fence_flags": forbidden_fence_flags,
        "selected_subject": p_case.selected_subject,
        "pretty_case_name": pretty_case_name(p_source, p_target),
        "pretty_cpp_file_name": pretty_cpp_file_name(p_source, p_target),
        "mutation": asdict(p_case.mutation),
        "archive_set_mutation": asdict(p_case.archive_set_mutation),
        "tree": {
            "files": [asdict(f) for f in p_case.tree.files],
            "empty_dirs": p_case.tree.empty_dirs,
        },
        "failure_point_comment": p_case.failure_point_comment,
        "notes": p_case.notes,
    }


def build_specs() -> List[Tuple[str, Optional[str]]]:
    specs: List[Tuple[str, Optional[str]]] = []
    for source in SOURCE_TYPES:
        # EOF trailing-garbage cannot be represented when payload bytes per L1 block is 1 (RH + 1).
        if source == "eof_gar" and engine.L1_PAYLOAD_LENGTH <= 1:
            continue
        if source in ("eof_gar", "eof_oob"):
            specs.append((source, None))
            continue
        for target in TARGET_TYPES:
            if source == "recovery_header" and target == "make_zero":
                continue
            specs.append((source, target))
    return specs


def generate_with_retries(rng: random.Random,
                          case_id: str,
                          size_class: str,
                          source: str,
                          target: Optional[str]) -> engine.TestCase:
    forced_target = forced_generation_target(target)
    last_error: Optional[Exception] = None
    size_attempts = [size_class]
    if size_class == "small":
        size_attempts.append("medium")

    previous_target_mode = engine.FAILURE_TARGET_TYPE
    if target in engine.TARGET_TYPES:
        engine.FAILURE_TARGET_TYPE = target
    else:
        engine.FAILURE_TARGET_TYPE = "any"

    try:
        for size_attempt in size_attempts:
            for _ in range(MAX_CASE_RETRIES):
                try:
                    case = engine.generate_case(
                        case_id=case_id,
                        size_class=size_attempt,
                        rng=rng,
                        forced_source=source,
                        forced_target=forced_target,
                    )
                    if target in engine.TARGET_TYPES and case.illegal_target_type != target:
                        last_error = RuntimeError(
                            f"Generated target '{case.illegal_target_type}' did not match requested '{target}'."
                        )
                        continue
                    return case
                except RuntimeError as exc:
                    last_error = exc
                    continue
    finally:
        engine.FAILURE_TARGET_TYPE = previous_target_mode

    raise RuntimeError(
        f"Failed to generate case after {MAX_CASE_RETRIES} attempts "
        f"(source={source}, target={target}, size={size_class}). Last error: {last_error}"
    )


def case_archive_count_and_layout(p_case: engine.TestCase) -> Tuple[int, engine.ArchiveLayout]:
    stream, _ = engine.build_fields(p_case.tree)
    layout = engine.ArchiveLayout(p_case.archive_payload_length)
    archive_count = max(1, (len(stream) + layout.logical_per_archive - 1) // layout.logical_per_archive)
    return archive_count, layout


def set_case_mutation_value(p_case: engine.TestCase, value: int) -> None:
    p_case.mutation.new_value = value
    p_case.mutation.new_value_le_hex = engine.le_hex(value, p_case.mutation.width_bytes)


def specialize_recovery_missing_archive_case(p_case: engine.TestCase) -> bool:
    stream, _ = engine.build_fields(p_case.tree)
    layout = engine.ArchiveLayout(p_case.archive_payload_length)
    file_lengths = engine.file_lengths_for_stream(layout, len(stream))
    if not file_lengths:
        return False

    # Anchor this family at archive[0], recovery block 0 distance field to avoid
    # unrelated decode-path fences masking the intended recovery-header fence.
    checksum_width = engine.RECOVERY_HEADER_LENGTH - engine.RECOVERY_DIST_WIDTH
    p_case.mutation.archive_index = 0
    p_case.mutation.offset = engine.PLAIN_HEADER_LENGTH + checksum_width
    p_case.mutation.payload_logical_offset = -1
    p_case.mutation.width_bytes = engine.RECOVERY_DIST_WIDTH
    p_case.selected_subject = "archive_0_recovery_header_at_block_0"
    header_end_abs = engine.PLAIN_HEADER_LENGTH + engine.RECOVERY_HEADER_LENGTH
    archive_file_len = file_lengths[0]

    # Generalized gap targeting:
    # 1) deterministically select a jump distance in [1..6]
    # 2) point the mutated stride into that future archive payload
    # 3) synthesize trailing archives so the gap can exist in the archive set
    jump_seed = f"{p_case.case_id}:{p_case.archive_payload_length}".encode("utf-8")
    jump = 1 + (int(hashlib.sha256(jump_seed).hexdigest()[:8], 16) % 6)
    gap_archive_index = jump
    target_archive_start_abs = gap_archive_index * archive_file_len
    target_abs = target_archive_start_abs + engine.PLAIN_HEADER_LENGTH + engine.RECOVERY_HEADER_LENGTH + 1
    distance = target_abs - header_end_abs
    if distance <= 0:
        return False

    max_value = (1 << (8 * p_case.mutation.width_bytes)) - 1
    if distance > max_value:
        return False

    set_case_mutation_value(p_case, distance)
    # Remove the selected jump archive and synthesize trailing archives so the
    # archive list can still extend beyond the gap.
    p_case.archive_set_mutation.remove_archive_indices = [gap_archive_index]
    create_indices = set(p_case.archive_set_mutation.create_archive_indices)
    create_indices.discard(gap_archive_index)
    current_max_index = max(0, len(file_lengths) - 1)
    required_max_index = gap_archive_index + 1
    if current_max_index < required_max_index:
        for idx in range(current_max_index + 1, required_max_index + 1):
            if idx == gap_archive_index:
                continue
            create_indices.add(idx)
    p_case.archive_set_mutation.create_archive_indices = sorted(create_indices)
    p_case.notes.append(
        "Target override: anchored recovery-distance mutation at archive[0] block-0, "
        f"selected deterministic jump={jump}, removed archive[{gap_archive_index}], "
        f"and synthesized trailing archives through index {required_max_index}."
    )
    return True


def apply_target_overrides(p_case: engine.TestCase, p_source: str, p_target: Optional[str]) -> bool:
    p_case.flow = preferred_flow(p_source, p_target)

    if p_source == "recovery_header" and p_target == "out_of_entire_archive_list_bounds":
        return specialize_recovery_missing_archive_case(p_case)

    if p_target == "out_of_entire_archive_list_bounds" and p_source not in ("eof_gar", "eof_oob"):
        # Force at least one missing archive index so gap-flag family has deterministic coverage.
        stream, _ = engine.build_fields(p_case.tree)
        layout = engine.ArchiveLayout(p_case.archive_payload_length)
        archive_count = max(1, (len(stream) + layout.logical_per_archive - 1) // layout.logical_per_archive)
        synthetic_index = archive_count + 1
        p_case.archive_set_mutation.create_archive_indices = [synthetic_index]
        p_case.notes.append(
            "Target override: added synthetic trailing archive to guarantee a missing-index gap for gap-fence assertions."
        )
    return True


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    run_engine_step()

    fmt = parse_format_constants(FORMAT_CONSTANTS_PATH)
    apply_format_constants_to_engine(fmt)
    # Disable strict target behavior from the base engine constants; this allows
    # it to grow/reshape trees while we enforce exact target matches here.
    engine.FAILURE_SOURCE_TYPE = "any"
    engine.FAILURE_TARGET_TYPE = "any"
    engine.FLOW_TYPE = "any"

    rng = random.Random(RANDOM_SEED)
    specs = build_specs()
    all_cases: List[Dict[str, object]] = []
    index_lines: List[str] = []

    for source, target in specs:
        pretty_name = pretty_case_name(source, target)
        pretty_cpp = pretty_cpp_file_name(source, target)
        total_for_spec = SMALL_CASES_PER_SPEC + MEDIUM_CASES_PER_SPEC

        for i in range(total_for_spec):
            size = "small" if i < SMALL_CASES_PER_SPEC else "medium"
            local_index = i if size == "small" else i - SMALL_CASES_PER_SPEC
            case_id = f"AUTO_{pretty_name}_{size.upper()}_{local_index:03d}"

            case: Optional[engine.TestCase] = None
            for _ in range(MAX_CASE_RETRIES):
                case = generate_with_retries(
                    rng=rng,
                    case_id=case_id,
                    size_class=size,
                    source=source,
                    target=target,
                )
                if case.size_class != size:
                    case.notes.append(
                        f"Requested size_class='{size}' was not representable; generated as '{case.size_class}'."
                    )
                if apply_target_overrides(case, source, target):
                    break
            else:
                raise RuntimeError(
                    f"Failed to apply specialization overrides for case {case_id} "
                    f"(source={source}, target={target})."
                )
            assert case is not None

            case_dict = to_case_dict(case, source, target, target)
            all_cases.append(case_dict)

            index_lines.append(
                f"{case_id} | {pretty_cpp} | source={source} | target={target or 'n/a'} "
                f"| flow={case.flow} | expected={case.expected_error_code}"
            )

    payload = {
        "version": 1,
        "seed": RANDOM_SEED,
        "config": {
            "source_types": SOURCE_TYPES,
            "target_types": TARGET_TYPES,
            "allowed_archive_block_counts": engine.ALLOWED_ARCHIVE_BLOCK_COUNTS,
            "small_cases_per_spec": SMALL_CASES_PER_SPEC,
            "medium_cases_per_spec": MEDIUM_CASES_PER_SPEC,
            "max_case_retries": MAX_CASE_RETRIES,
            "derived_from": "test_case_gen_engine.py",
            "plain_header_length": engine.PLAIN_HEADER_LENGTH,
            "recovery_header_length": engine.RECOVERY_HEADER_LENGTH,
            "l1_payload_length": engine.L1_PAYLOAD_LENGTH,
            "l1_length": engine.L1_LENGTH,
            "sb_l3_length_default": engine.SB_L3_LENGTH_DEFAULT,
            "recovery_dist_width": engine.RECOVERY_DIST_WIDTH,
        },
        "cases": all_cases,
    }

    OUTPUT_JSON.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    OUTPUT_INDEX.write_text("\n".join(index_lines) + "\n", encoding="utf-8")

    print(f"Synced constants from {FORMAT_CONSTANTS_PATH}:")
    print(f"  SB_PLAIN_TEXT_HEADER_LENGTH={engine.PLAIN_HEADER_LENGTH}")
    print(f"  SB_RECOVERY_HEADER_LENGTH={engine.RECOVERY_HEADER_LENGTH}")
    print(f"  SB_PAYLOAD_SIZE(test)={engine.L1_PAYLOAD_LENGTH}")
    print("")
    print(f"Wrote {len(all_cases)} cases to {OUTPUT_JSON}")
    print(f"Wrote index to {OUTPUT_INDEX}")
    block_counts = sorted({int(c["archive_block_count"]) for c in all_cases})
    print(f"Archive block counts used: {block_counts}")
    run_executor_cpp_step()
    print(f"Generated C++ fence tests from {OUTPUT_JSON} into {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
