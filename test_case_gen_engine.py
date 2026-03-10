#!/usr/bin/env python3
"""
Examples:
  python3 test_case_gen_engine.py --failure-source-type any --failure-target-type any --flow-type any
  python3 test_case_gen_executor_readable.py
  python3 test_case_gen_executor_cpp.py
"""
from __future__ import annotations

import argparse
import ast
import json
import random
import re
import string
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# Configuration defaults (CLI can override)
# Valid: file_name_length | file_content_length | directory_name_length | recovery_header | eof_gar | eof_oob | any
FAILURE_SOURCE_TYPE = "file_name_length"
# Valid: out_of_entire_archive_list_bounds | out_of_this_archive_bounds | within_recovery_header | within_archive_header | make_zero | any
FAILURE_TARGET_TYPE = "within_recovery_header"
# Valid: unbundle | recover | any
FLOW_TYPE = "any"
INCLUDE_EOF_SOURCE_TYPES_IN_ANY = False

SMALL_COUNT = 1
MEDIUM_COUNT = 0

FUDGE_LOOPS = 4096
RETRY_LOOPS = 256

RANDOM_SEED = 1337
OUTPUT_JSON = "tests/generated/test_cases.json"
FORMAT_CONSTANTS_PATH = "src/FormatConstants.hpp"
SYNC_FORMAT_CONSTANTS = True

PLAIN_HEADER_LENGTH = 40
RECOVERY_HEADER_LENGTH = 16
# L1_PAYLOAD_LENGTH = 88
L1_PAYLOAD_LENGTH = RECOVERY_HEADER_LENGTH + 1

L1_LENGTH = L1_PAYLOAD_LENGTH + RECOVERY_HEADER_LENGTH
SB_L3_LENGTH_DEFAULT = L1_LENGTH * 4
ARCHIVE_TARGET_L3_BLOCKS_LO = 1
ARCHIVE_TARGET_L3_BLOCKS_HI = 4
ALLOWED_ARCHIVE_BLOCK_COUNTS = [1, 2, 3, 4]

MAX_CONTENT_LENGTH_SMALL = 16
MAX_CONTENT_LENGTH_MEDIUM = 80
MAX_NAME_LENGTH_SMALL = 4
MAX_NAME_LENGTH_MEDIUM = 36

PATH_LEN_WIDTH = 2
CONTENT_LEN_WIDTH = 6
RECOVERY_DIST_WIDTH = 8


SOURCE_TYPES = ["file_name_length", "file_content_length", "directory_name_length", "recovery_header", "eof_gar", "eof_oob"]
TARGET_TYPES = [
    "out_of_entire_archive_list_bounds",
    "out_of_this_archive_bounds",
    "within_recovery_header",
    "within_archive_header",
    "make_zero",
]
FLOW_TYPES = ["unbundle", "recover"]


def _parse_constants_from_format_constants(path: str) -> Dict[str, int]:
    text = Path(path).read_text(encoding="utf-8")

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
            raise RuntimeError("Could not find PEANUT_BUTTER_ULTIMA_TEST_BUILD block in FormatConstants.hpp.")
        return match.group(1)

    def _parse_named(name: str) -> int:
        match = re.search(rf"{name}\s*=\s*([0-9]+)\s*;", text)
        if not match:
            raise RuntimeError(f"Could not parse {name} from {path}")
        return int(match.group(1))

    plain_header = _parse_named("SB_PLAIN_TEXT_HEADER_LENGTH")
    checksum = _parse_named("SB_RECOVERY_CHECKSUM_LENGTH")
    stride = _parse_named("SB_RECOVERY_STRIDE_LENGTH")

    symbols = {
        "SB_PLAIN_TEXT_HEADER_LENGTH": plain_header,
        "SB_RECOVERY_CHECKSUM_LENGTH": checksum,
        "SB_RECOVERY_STRIDE_LENGTH": stride,
        "SB_RECOVERY_HEADER_LENGTH": checksum + stride,
        "EB_MAX_LENGTH": _parse_named("EB_MAX_LENGTH"),
    }
    test_block = _strip_cpp_comments(_extract_test_build_block(text))
    payload_match = re.search(r"SB_PAYLOAD_SIZE\s*=\s*([^;]+)\s*;", test_block)
    if not payload_match:
        raise RuntimeError(
            "Could not parse SB_PAYLOAD_SIZE from active test-build branch in FormatConstants.hpp"
        )
    payload = _eval_int_expr(payload_match.group(1).strip(), symbols)

    return {
        "plain_header_length": plain_header,
        "recovery_header_length": checksum + stride,
        "l1_payload_length": payload,
        "recovery_distance_width": stride,
    }


def _sync_runtime_constants(path: str) -> None:
    global PLAIN_HEADER_LENGTH
    global RECOVERY_HEADER_LENGTH
    global L1_PAYLOAD_LENGTH
    global L1_LENGTH
    global SB_L3_LENGTH_DEFAULT
    global RECOVERY_DIST_WIDTH

    values = _parse_constants_from_format_constants(path)
    PLAIN_HEADER_LENGTH = values["plain_header_length"]
    RECOVERY_HEADER_LENGTH = values["recovery_header_length"]
    L1_PAYLOAD_LENGTH = values["l1_payload_length"]
    L1_LENGTH = L1_PAYLOAD_LENGTH + RECOVERY_HEADER_LENGTH
    SB_L3_LENGTH_DEFAULT = L1_LENGTH * 4
    RECOVERY_DIST_WIDTH = values["recovery_distance_width"]


def choose_from_mode(rng: random.Random, mode: str, allowed: List[str]) -> str:
    if mode == "any":
        return rng.choice(allowed)
    if mode not in allowed:
        raise ValueError(f"Invalid configured mode '{mode}'. Allowed: {allowed + ['any']}")
    return mode


def source_types_for_any() -> List[str]:
    base = ["file_name_length", "directory_name_length", "file_content_length", "recovery_header"]
    if INCLUDE_EOF_SOURCE_TYPES_IN_ANY:
        base.extend(["eof_gar", "eof_oob"])
    return base


def le_hex(value: int, width: int) -> str:
    return bytes((value >> (8 * i)) & 0xFF for i in range(width)).hex()


def random_alpha(rng: random.Random, lo: int, hi: int) -> str:
    lo = max(1, lo)
    hi = max(lo, hi)
    n = rng.randint(lo, hi)
    chars = string.ascii_letters
    return "".join(rng.choice(chars) for _ in range(n))


def random_name(rng: random.Random, max_len: int) -> str:
    ext = rng.choice([".txt", ".bin", ".dat", ".cfg", ".md"])
    stem_max = max(1, max_len - len(ext))
    stem = random_alpha(rng, 1, stem_max)
    return stem + ext


def normalize_empty_dirs(dirs: List[str]) -> List[str]:
    norm = sorted(set(d.strip("/").replace("//", "/") for d in dirs if d.strip("/")))
    keep: List[str] = []
    for d in norm:
        if not any(k.startswith(d + "/") for k in norm):
            keep.append(d)
    return keep


def effective_empty_dirs(tree: TreeSpec) -> List[str]:
    dirs = normalize_empty_dirs(tree.empty_dirs)
    file_paths = [f.path.strip("/") for f in tree.files]
    keep: List[str] = []
    for d in dirs:
        d_norm = d.strip("/")
        if not d_norm:
            continue
        if any(fp == d_norm or fp.startswith(d_norm + "/") for fp in file_paths):
            continue
        keep.append(d_norm)
    return keep


def append_unique_file(tree: TreeSpec,
                       rng: random.Random,
                       max_name_len: int,
                       max_content_len: int,
                       prefer_max_bytes: bool = False) -> None:
    existing = {f.path for f in tree.files}
    for _ in range(RETRY_LOOPS):
        if prefer_max_bytes:
            depth = 2
            segs = [random_alpha(rng, max_name_len, max_name_len) for _ in range(depth)]
            rel = "/".join(segs + [random_name(rng, max_name_len)])
            content = random_alpha(rng, max_content_len, max_content_len)
        else:
            depth = rng.randint(0, 2)
            segs = [random_alpha(rng, 1, max_name_len) for _ in range(depth)]
            rel = "/".join(segs + [random_name(rng, max_name_len)])
            content = random_alpha(rng, 1, max_content_len)
        if rel in existing:
            continue
        tree.files.append(FileEntry(path=rel, content=content))
        return
    raise RuntimeError("Unable to generate unique file path.")


def append_unique_dir(tree: TreeSpec, rng: random.Random, max_name_len: int) -> None:
    existing = set(tree.empty_dirs)
    for _ in range(RETRY_LOOPS):
        depth = rng.randint(1, 3)
        segs = [random_alpha(rng, 1, max_name_len) for _ in range(depth)]
        rel = "/".join(segs)
        if rel in existing:
            continue
        tree.empty_dirs.append(rel)
        return
    raise RuntimeError("Unable to generate unique directory path.")


@dataclass
class FileEntry:
    path: str
    content: str


@dataclass
class TreeSpec:
    files: List[FileEntry]
    empty_dirs: List[str]


@dataclass
class FieldRef:
    field_kind: str
    subject_path: str
    width_bytes: int
    logical_offset: int
    logical_end_offset: int
    span_length: int


@dataclass
class Mutation:
    archive_index: int
    offset: int
    width_bytes: int
    new_value: int
    new_value_le_hex: str


@dataclass
class ArchiveSetMutation:
    create_archive_indices: List[int]
    remove_archive_indices: List[int]


@dataclass
class TestCase:
    case_id: str
    size_class: str
    flow: str
    archive_payload_length: int
    l1_length: int
    field_kind: str
    illegal_target_type: str
    expected_error_code: str
    selected_subject: str
    selected_file_record_index: int
    mutation: Mutation
    archive_set_mutation: ArchiveSetMutation
    tree: TreeSpec
    recoverable_files: List[FileEntry]
    failure_point_comment: str
    notes: List[str]


class ArchiveLayout:
    def __init__(self, payload_length: int) -> None:
        self.payload_length = payload_length
        self.l1_length = L1_LENGTH
        self.plain_header_len = PLAIN_HEADER_LENGTH
        self.recovery_header_len = RECOVERY_HEADER_LENGTH
        self.data_per_block = self.l1_length - self.recovery_header_len
        full_blocks = self.payload_length // self.l1_length
        rem = self.payload_length % self.l1_length
        rem_data = max(0, rem - self.recovery_header_len)
        self.logical_per_archive = full_blocks * self.data_per_block + rem_data

    def logical_to_physical(self, logical_offset: int) -> Tuple[int, int]:
        archive_index = logical_offset // self.logical_per_archive
        in_archive_logical = logical_offset % self.logical_per_archive
        block_index = in_archive_logical // self.data_per_block
        in_block_data = in_archive_logical % self.data_per_block
        payload_off = block_index * self.l1_length + self.recovery_header_len + in_block_data
        file_off = self.plain_header_len + payload_off
        return archive_index, file_off

    def abs_pos(self, archive_index: int, file_offset: int) -> int:
        return archive_index * (self.plain_header_len + self.payload_length) + file_offset

    def recovery_headers_per_archive(self) -> int:
        return self.payload_length // self.l1_length

    def recovery_header_at(self, archive_index: int, block_index: int) -> Tuple[int, int, int]:
        file_off = self.plain_header_len + (block_index * self.l1_length)
        return archive_index, file_off, self.abs_pos(archive_index, file_off)

    def archive_header_abs_ranges(self, archive_count: int) -> List[Tuple[int, int, int]]:
        out: List[Tuple[int, int, int]] = []
        archive_file_len = self.plain_header_len + self.payload_length
        for a in range(archive_count):
            start = a * archive_file_len
            out.append((a, start, start + self.plain_header_len - 1))
        return out


def estimate_smallest_feasible_payload_multiplier(max_name_len: int,
                                                  max_content_len: int,
                                                  required_archives: int) -> List[int]:
    # Upper-bound the stream size reachable under configured growth limits.
    per_file_bytes_upper = 2 + (3 * max_name_len + 12) + 6 + max_content_len
    max_files_upper = (FUDGE_LOOPS * 16) + 64
    logical_budget = per_file_bytes_upper * max_files_upper
    feasible: List[int] = []
    lo = max(min(ALLOWED_ARCHIVE_BLOCK_COUNTS), ARCHIVE_TARGET_L3_BLOCKS_LO)
    hi = min(max(ALLOWED_ARCHIVE_BLOCK_COUNTS), ARCHIVE_TARGET_L3_BLOCKS_HI)
    if lo > hi:
        return []
    for mult in ALLOWED_ARCHIVE_BLOCK_COUNTS:
        if mult < lo or mult > hi:
            continue
        layout = ArchiveLayout(SB_L3_LENGTH_DEFAULT * mult)
        if required_archives * layout.logical_per_archive <= logical_budget:
            feasible.append(mult)
    return feasible


def build_tree(rng: random.Random, size_class: str) -> TreeSpec:
    if size_class == "small":
        file_count = rng.randint(0, 6)
        dir_count = rng.randint(0, 4)
        max_content_len = MAX_CONTENT_LENGTH_SMALL
        max_name_len = MAX_NAME_LENGTH_SMALL
    else:
        file_count = rng.randint(0, 18)
        dir_count = rng.randint(0, 10)
        max_content_len = MAX_CONTENT_LENGTH_MEDIUM
        max_name_len = MAX_NAME_LENGTH_MEDIUM

    tree = TreeSpec(files=[], empty_dirs=[])
    for _ in range(file_count):
        append_unique_file(tree, rng, max_name_len, max_content_len)

    for _ in range(dir_count):
        append_unique_dir(tree, rng, max_name_len)
    tree.empty_dirs = normalize_empty_dirs(tree.empty_dirs)
    return tree


def ensure_pickable(tree: TreeSpec, field_kind: str, rng: random.Random, max_name_len: int, max_content_len: int) -> None:
    if field_kind in ("file_name_length", "file_content_length") and not tree.files:
        append_unique_file(tree, rng, max_name_len, max_content_len)
    if field_kind == "directory_name_length" and not tree.empty_dirs:
        append_unique_dir(tree, rng, max_name_len)


def build_fields(tree: TreeSpec) -> Tuple[bytes, Dict[str, List[FieldRef]]]:
    fields: Dict[str, List[FieldRef]] = {
        "file_name_length": [],
        "file_content_length": [],
        "directory_name_length": [],
    }
    out = bytearray()
    for f in sorted(tree.files, key=lambda x: x.path):
        path_bytes = f.path.encode("utf-8")
        path_off = len(out)
        out += len(path_bytes).to_bytes(2, "little")
        fields["file_name_length"].append(
            FieldRef("file_name_length", f.path, PATH_LEN_WIDTH, path_off, path_off + 2, len(path_bytes))
        )
        out += path_bytes
        content_off = len(out)
        content_bytes = f.content.encode("utf-8")
        out += len(content_bytes).to_bytes(6, "little")
        fields["file_content_length"].append(
            FieldRef("file_content_length", f.path, CONTENT_LEN_WIDTH, content_off, content_off + 6, len(content_bytes))
        )
        out += content_bytes
    for d in effective_empty_dirs(tree):
        d_bytes = d.encode("utf-8")
        d_off = len(out)
        out += len(d_bytes).to_bytes(2, "little")
        fields["directory_name_length"].append(
            FieldRef("directory_name_length", d, PATH_LEN_WIDTH, d_off, d_off + 2, len(d_bytes))
        )
        out += d_bytes
        out += ((1 << 48) - 1).to_bytes(6, "little")
    out += (0).to_bytes(2, "little")
    return bytes(out), fields


def selectable_integer_refs(layout: ArchiveLayout, refs: List[FieldRef]) -> List[FieldRef]:
    # Test harness archive-byte mutations write contiguous physical bytes.
    # Keep only integer fields whose encoded bytes are physically contiguous in payload.
    return [ref for ref in refs if integer_fits_without_crossing_header(layout, ref.logical_offset, ref.width_bytes)]


def collect_zero_marker_offsets(tree: TreeSpec) -> List[int]:
    offset = 0
    for f in sorted(tree.files, key=lambda x: x.path):
        offset += 2
        offset += len(f.path.encode("utf-8"))
        offset += 6
        offset += len(f.content.encode("utf-8"))
    for d in effective_empty_dirs(tree):
        offset += 2
        offset += len(d.encode("utf-8"))
        offset += 6
    stream_terminator_offset = offset
    return [stream_terminator_offset]


def logical_bytes_per_archive(layout: ArchiveLayout, stream_len: int) -> List[int]:
    archive_count = max(1, (stream_len + layout.logical_per_archive - 1) // layout.logical_per_archive)
    out: List[int] = []
    remaining = max(0, stream_len)
    for _ in range(archive_count):
        logical = min(layout.logical_per_archive, remaining)
        out.append(logical)
        remaining -= logical
    return out


def payload_bytes_for_logical(layout: ArchiveLayout, logical_bytes: int) -> int:
    if logical_bytes <= 0:
        return 0
    last_logical = logical_bytes - 1
    block_index = last_logical // layout.data_per_block
    in_block = last_logical % layout.data_per_block
    last_physical = (block_index * layout.l1_length) + RECOVERY_HEADER_LENGTH + in_block
    # Match runtime PhysicalLengthForLogicalLength: round to page (SB_L3) boundaries.
    return ((last_physical + 1 + SB_L3_LENGTH_DEFAULT - 1) // SB_L3_LENGTH_DEFAULT) * SB_L3_LENGTH_DEFAULT


def file_lengths_for_stream(layout: ArchiveLayout, stream_len: int) -> List[int]:
    logical_parts = logical_bytes_per_archive(layout, stream_len)
    return [PLAIN_HEADER_LENGTH + payload_bytes_for_logical(layout, logical) for logical in logical_parts]


def file_start_abs_positions(file_lengths: List[int]) -> List[int]:
    starts: List[int] = []
    cursor = 0
    for length in file_lengths:
        starts.append(cursor)
        cursor += length
    return starts


def recovery_header_abs_positions(layout: ArchiveLayout, stream_len: int) -> List[int]:
    logical_parts = logical_bytes_per_archive(layout, stream_len)
    lengths = file_lengths_for_stream(layout, stream_len)
    starts = file_start_abs_positions(lengths)
    out: List[int] = []
    for archive_index, logical in enumerate(logical_parts):
        payload_len = lengths[archive_index] - PLAIN_HEADER_LENGTH
        if payload_len <= 0:
            continue
        used_blocks = payload_len // layout.l1_length
        for block_index in range(used_blocks):
            file_off = PLAIN_HEADER_LENGTH + (block_index * layout.l1_length)
            out.append(starts[archive_index] + file_off)
    return out


def choose_illegal_value(
    rng: random.Random,
    layout: ArchiveLayout,
    stream_len: int,
    start_archive: int,
    start_payload_off: int,
    width_bytes: int,
    illegal_target_type: str,
    min_value: int = 1,
) -> Optional[int]:
    max_value = (1 << (8 * width_bytes)) - 1
    file_lengths = file_lengths_for_stream(layout, stream_len)
    if start_archive < 0 or start_archive >= len(file_lengths):
        return None
    if start_payload_off < 0 or start_payload_off >= layout.payload_length:
        return None

    starts = file_start_abs_positions(file_lengths)
    start_abs = starts[start_archive] + PLAIN_HEADER_LENGTH + start_payload_off
    total_bytes = sum(file_lengths)
    if start_abs >= total_bytes:
        return None
    remaining_total = total_bytes - start_abs - 1

    def choose_by_absolute_targets(target_abs_positions: List[int]) -> Optional[int]:
        candidates: List[int] = []
        for target_abs in target_abs_positions:
            delta = target_abs - start_abs
            if min_value <= delta <= max_value:
                candidates.append(delta)
        if not candidates:
            return None
        return rng.choice(candidates)

    if illegal_target_type == "out_of_entire_archive_list_bounds":
        lo = max(remaining_total + 1, min_value)
        if lo > max_value:
            return None
        return rng.randint(lo, max_value)

    if illegal_target_type == "out_of_this_archive_bounds":
        archive_end_abs = starts[start_archive] + file_lengths[start_archive] - 1
        remaining_this_archive = max(0, archive_end_abs - start_abs)
        lo = max(remaining_this_archive + 1, min_value)
        if lo > max_value:
            return None
        return rng.randint(lo, max_value)

    if illegal_target_type == "within_recovery_header":
        targets: List[int] = []
        for archive_index, file_length in enumerate(file_lengths):
            payload_length = max(0, file_length - PLAIN_HEADER_LENGTH)
            if payload_length == 0:
                continue
            archive_start_abs = starts[archive_index]
            for block_start in range(0, payload_length, layout.l1_length):
                rec_start_abs = archive_start_abs + PLAIN_HEADER_LENGTH + block_start
                rec_end_abs = min(
                    rec_start_abs + RECOVERY_HEADER_LENGTH,
                    archive_start_abs + file_length,
                )
                for abs_pos in range(rec_start_abs, rec_end_abs):
                    targets.append(abs_pos)
        return choose_by_absolute_targets(targets)

    if illegal_target_type == "within_archive_header":
        targets: List[int] = []
        for archive_index, file_length in enumerate(file_lengths):
            archive_start_abs = starts[archive_index]
            header_end_abs = min(archive_start_abs + PLAIN_HEADER_LENGTH, archive_start_abs + file_length)
            for abs_pos in range(archive_start_abs, header_end_abs):
                targets.append(abs_pos)
        return choose_by_absolute_targets(targets)

    return None


def span_fits_without_crossing_header(layout: ArchiveLayout, start_logical: int, span_len: int) -> bool:
    if span_len == 0:
        return True
    archive_index, file_off = layout.logical_to_physical(start_logical)
    payload_off = file_off - PLAIN_HEADER_LENGTH
    if payload_off >= layout.payload_length:
        return False
    offset_in_block = payload_off % layout.l1_length
    if offset_in_block < RECOVERY_HEADER_LENGTH:
        return False
    remaining_to_recovery = layout.l1_length - offset_in_block
    remaining_to_archive_end = layout.payload_length - payload_off
    safe_remaining = min(remaining_to_recovery, remaining_to_archive_end)
    return span_len < safe_remaining


def integer_fits_without_crossing_header(layout: ArchiveLayout, start_logical: int, width: int) -> bool:
    if width == 0:
        return True
    archive_index, file_off = layout.logical_to_physical(start_logical)
    payload_off = file_off - PLAIN_HEADER_LENGTH
    if payload_off >= layout.payload_length:
        return False
    offset_in_block = payload_off % layout.l1_length
    if offset_in_block < RECOVERY_HEADER_LENGTH:
        return False
    remaining_to_recovery = layout.l1_length - offset_in_block
    remaining_to_archive_end = layout.payload_length - payload_off
    safe_remaining = min(remaining_to_recovery, remaining_to_archive_end)
    return width <= safe_remaining


def is_clean_baseline_tree(layout: ArchiveLayout, tree: TreeSpec) -> bool:
    _, fields = build_fields(tree)
    for refs in fields.values():
        for ref in refs:
            if not integer_fits_without_crossing_header(layout, ref.logical_offset, ref.width_bytes):
                return False
            if ref.span_length <= 0:
                continue
            if not span_fits_without_crossing_header(layout, ref.logical_end_offset, ref.span_length):
                return False
    for marker_offset in collect_zero_marker_offsets(tree):
        if not integer_fits_without_crossing_header(layout, marker_offset, 2):
            return False
    return True


def make_failure_comment(field_kind: str, illegal_target_type: str) -> str:
    source = {
        "file_name_length": "Illegal file name length",
        "file_content_length": "Illegal file content length",
        "directory_name_length": "Illegal directory name length",
        "recovery_header": "Illegal recovery-header distance",
        "eof_gar": "EOF garbage case",
        "eof_oob": "EOF out-of-bounds archive tail case",
    }[field_kind]
    if field_kind in ("eof_gar", "eof_oob"):
        return source + ", target type ignored for EOF source mode."
    target = {
        "out_of_entire_archive_list_bounds": "pointing outside entire archive list",
        "out_of_this_archive_bounds": "pointing outside current archive",
        "within_recovery_header": "pointing into a recovery header",
        "within_archive_header": "pointing into an archive header",
        "make_zero": "forced to zero value",
    }[illegal_target_type]
    return f"{source}, modified to be {target}."


def expected_error_code(field_kind: str, illegal_target_type: str) -> str:
    if illegal_target_type == "make_zero":
        if field_kind in ("file_name_length", "directory_name_length", "file_content_length"):
            return "UNP_EOF_003"
        if field_kind == "recovery_header":
            return "UNP_EOF_003"
    if field_kind in ("file_name_length", "directory_name_length"):
        return "UNP_FNL_FENCE"
    if field_kind == "file_content_length":
        return "UNP_FDL_FENCE"
    if field_kind == "recovery_header":
        return "UNP_RHD_FENCE"
    if field_kind == "eof_gar":
        return "UNP_EOF_002"
    if field_kind == "eof_oob":
        return "UNP_EOF_001"
    return "UNK_SYS_001"


def generate_case(case_id: str,
                  size_class: str,
                  rng: random.Random,
                  forced_source: Optional[str] = None,
                  forced_target: Optional[str] = None) -> TestCase:
    field_kind = forced_source if forced_source is not None else choose_from_mode(rng, FAILURE_SOURCE_TYPE, SOURCE_TYPES)
    illegal_target_type = forced_target if forced_target is not None else choose_from_mode(rng, FAILURE_TARGET_TYPE, TARGET_TYPES)
    flow = choose_from_mode(rng, FLOW_TYPE, FLOW_TYPES)
    strict_target = FAILURE_TARGET_TYPE != "any"

    if field_kind == "recovery_header":
        # Recovery-header failure source is recover-flow specific.
        flow = "recover"
    if illegal_target_type == "make_zero":
        flow = "recover"
    if field_kind in ("eof_gar", "eof_oob"):
        illegal_target_type = "ignored_for_eof"

    max_name_len = MAX_NAME_LENGTH_SMALL if size_class == "small" else MAX_NAME_LENGTH_MEDIUM
    max_content_len = MAX_CONTENT_LENGTH_SMALL if size_class == "small" else MAX_CONTENT_LENGTH_MEDIUM

    required_archives = 1
    if field_kind == "recovery_header" and illegal_target_type == "within_archive_header":
        required_archives = 3
    feasible_multipliers = estimate_smallest_feasible_payload_multiplier(
        max_name_len=max_name_len,
        max_content_len=max_content_len,
        required_archives=required_archives,
    )
    if field_kind == "recovery_header" and illegal_target_type == "within_archive_header":
        # Keep this conservative so strict recovery-header -> archive-header cases are always constructible.
        if size_class == "small":
            feasible_multipliers = [m for m in feasible_multipliers if m == 1]
        else:
            feasible_multipliers = [m for m in feasible_multipliers if m in (1, 2)]
    if not feasible_multipliers:
        raise RuntimeError(
            "No feasible ARCHIVE_PAYLOAD_LENGTH multiplier for configured limits. "
            "Increase FUDGE_LOOPS or reduce L1/payload constraints."
        )
    archive_payload_length = SB_L3_LENGTH_DEFAULT * rng.choice(feasible_multipliers)
    layout = ArchiveLayout(archive_payload_length)
    tree = build_tree(rng, size_class)
    if field_kind != "directory_name_length":
        tree.empty_dirs = []
    ensure_pickable(tree, field_kind, rng, max_name_len, max_content_len)
    if not tree.files:
        append_unique_file(tree, rng, max_name_len, max_content_len)
    notes: List[str] = []
    archive_set_mutation = ArchiveSetMutation(create_archive_indices=[], remove_archive_indices=[])

    stream, fields = build_fields(tree)
    archive_count = max(1, (len(stream) + layout.logical_per_archive - 1) // layout.logical_per_archive)

    if field_kind in ("eof_gar", "eof_oob"):
        # Keep EOF-source cases in a single logical archive so EOF classification is
        # about dangling bytes/archive-tail mutation, not incidental multi-archive drift.
        while len(stream) > layout.logical_per_archive and len(tree.files) > 1:
            tree.files = sorted(tree.files, key=lambda x: x.path)[:-1]
            stream, fields = build_fields(tree)
            notes.append("Trimmed EOF source tree to keep logical stream within one archive.")

        if len(stream) > layout.logical_per_archive:
            # Tiny payload geometry can leave a single random medium file still too large.
            # Fall back to a compact deterministic tree that is guaranteed to fit.
            tree = TreeSpec(
                files=[
                    FileEntry(path="a.txt", content="a"),
                    FileEntry(path="b.txt", content="b"),
                ],
                empty_dirs=[],
            )
            stream, fields = build_fields(tree)
            notes.append("EOF source fallback: replaced oversized tree with compact deterministic file set.")
            if len(stream) > layout.logical_per_archive:
                raise RuntimeError(f"Could not constrain EOF source stream to one archive for {case_id}")

        ensure_pickable(tree, "file_name_length", rng, max_name_len, max_content_len)
        stream, fields = build_fields(tree)
        picks = selectable_integer_refs(
            layout,
            [entry for entry in fields["file_name_length"] if entry.span_length > 0],
        )
        grow_loops = 0
        while not picks:
            if grow_loops >= FUDGE_LOOPS:
                raise RuntimeError(f"Could not create EOF anchor field for {case_id}")
            append_unique_file(tree, rng, max_name_len, max_content_len, prefer_max_bytes=True)
            stream, fields = build_fields(tree)
            picks = selectable_integer_refs(
                layout,
                [entry for entry in fields["file_name_length"] if entry.span_length > 0],
            )
            grow_loops += 1
            notes.append("Expanded tree to obtain EOF mutation anchor path length.")

        # Prefer a non-terminal file-name length so path_length=0 becomes an early logical end marker.
        ref = picks[-1]

        # Sentinel rule:
        # If the selected path-length field lands on archive boundary edge conditions, append a sentinel
        # file and retry so the resulting early end marker has deterministic trailing bytes.
        sentinel_loops = 0
        while True:
            # Keep one full record byte after the mutated length field inside the same archive when possible.
            after_len = ref.logical_end_offset
            if (after_len % layout.logical_per_archive) != 0:
                break
            if sentinel_loops >= FUDGE_LOOPS:
                raise RuntimeError(f"Could not place EOF sentinel file away from archive boundary for {case_id}")
            tree.files.append(
                FileEntry(
                    path=f"z{sentinel_loops:03d}.t",
                    content="s",
                )
            )
            tree.files = sorted(tree.files, key=lambda x: x.path)
            stream, fields = build_fields(tree)
            picks = selectable_integer_refs(
                layout,
                [entry for entry in fields["file_name_length"] if entry.span_length > 0],
            )
            if not picks:
                raise RuntimeError(f"EOF sentinel insertion removed selectable path-length fields for {case_id}")
            ref = picks[-2] if len(picks) >= 2 else picks[-1]
            sentinel_loops += 1
            notes.append("Added EOF sentinel file to avoid archive-boundary terminal EOF mutation.")

        archive_index, offset = layout.logical_to_physical(ref.logical_offset)
        width = ref.width_bytes
        subject = ref.subject_path
        value = 0
        if field_kind == "eof_oob":
            archive_count = max(1, (len(stream) + layout.logical_per_archive - 1) // layout.logical_per_archive)
            archive_set_mutation.create_archive_indices = [archive_count]
            notes.append("EOF out-of-bounds mode: added trailing archive to force UNP_EOF_001 classification.")
        notes.append("EOF source mode active: ignored configured target type.")
    elif field_kind == "recovery_header":
        grow_loops = 0
        source_archives: List[int] = []
        used_blocks_by_archive: List[int] = []
        while True:
            logical_parts = logical_bytes_per_archive(layout, len(stream))
            archive_count = len(logical_parts)
            used_blocks_by_archive = [
                (payload_bytes_for_logical(layout, logical) // layout.l1_length) if logical > 0 else 0
                for logical in logical_parts
            ]

            source_archives = [idx for idx, used_blocks in enumerate(used_blocks_by_archive) if used_blocks > 0]
            if illegal_target_type == "within_archive_header":
                source_archives = [idx for idx in source_archives if idx + 1 < archive_count]

            if source_archives:
                break
            if grow_loops >= FUDGE_LOOPS:
                raise RuntimeError(f"Could not create selectable recovery header for {case_id}")
            for _ in range(16):
                append_unique_file(tree, rng, max_name_len, max_content_len, prefer_max_bytes=True)
            stream, fields = build_fields(tree)
            grow_loops += 1
            notes.append("Expanded tree to obtain selectable recovery header candidate.")

        # Use a deterministic, first-reachable recovery header anchor so recover flow
        # reads and fences this header before any EOF fallback path can hide it.
        archive_index = source_archives[0]
        block_index = 0
        recovery_checksum_width = RECOVERY_HEADER_LENGTH - RECOVERY_DIST_WIDTH
        offset = PLAIN_HEADER_LENGTH + (block_index * layout.l1_length) + recovery_checksum_width
        width = RECOVERY_DIST_WIDTH
        subject = f"archive_{archive_index}_recovery_header_at_{offset}"
        start_archive = archive_index
        start_off = (block_index * layout.l1_length) + RECOVERY_HEADER_LENGTH
    else:
        picks = selectable_integer_refs(layout, fields[field_kind])
        grow_loops = 0
        while not picks:
            if grow_loops >= FUDGE_LOOPS:
                raise RuntimeError(f"Could not create selectable field target for {case_id}")
            if field_kind in ("file_name_length", "file_content_length"):
                append_unique_file(tree, rng, max_name_len, max_content_len)
            else:
                append_unique_dir(tree, rng, max_name_len)
            stream, fields = build_fields(tree)
            picks = selectable_integer_refs(layout, fields[field_kind])
            archive_count = max(1, (len(stream) + layout.logical_per_archive - 1) // layout.logical_per_archive)
            grow_loops += 1
            notes.append("Expanded tree to obtain selectable field target.")
        ref = rng.choice(picks)
        archive_index, offset = layout.logical_to_physical(ref.logical_offset)
        start_archive, start_end_file_off = layout.logical_to_physical(ref.logical_end_offset)
        start_off = start_end_file_off - PLAIN_HEADER_LENGTH
        width = ref.width_bytes
        subject = ref.subject_path

    if field_kind not in ("eof_gar", "eof_oob"):
        if illegal_target_type == "make_zero":
            value = 0
        else:
            value = None
        min_illegal_value = 1

        def refresh_selected_field() -> bool:
            nonlocal ref
            nonlocal archive_index
            nonlocal offset
            nonlocal start_archive
            nonlocal start_off
            nonlocal width
            nonlocal subject
            nonlocal min_illegal_value

            if field_kind not in ("file_name_length", "directory_name_length", "file_content_length"):
                return True

            current_picks = selectable_integer_refs(layout, fields[field_kind])
            if not current_picks:
                return False

            selected = None
            for candidate in current_picks:
                if candidate.subject_path == subject and candidate.width_bytes == width:
                    selected = candidate
                    break
            if selected is None:
                selected = rng.choice(current_picks)
                if selected.subject_path != subject:
                    notes.append(
                        f"Mutation subject shifted from '{subject}' to '{selected.subject_path}' while satisfying constraints."
                    )

            ref = selected
            archive_index, offset = layout.logical_to_physical(ref.logical_offset)
            start_archive, start_end_file_off = layout.logical_to_physical(ref.logical_end_offset)
            start_off = start_end_file_off - PLAIN_HEADER_LENGTH
            width = ref.width_bytes
            subject = ref.subject_path

            total_logical_capacity = archive_count * layout.logical_per_archive
            min_illegal_value = max(1, (total_logical_capacity - ref.logical_end_offset) + 1)
            return True

        if not refresh_selected_field():
            raise RuntimeError(f"Could not refresh selected field for {case_id}")

        if illegal_target_type != "make_zero":
            target_order = [illegal_target_type]
            if not strict_target:
                target_order.extend(["within_recovery_header", "within_archive_header",
                                     "out_of_entire_archive_list_bounds", "out_of_this_archive_bounds"])
            dedupe: List[str] = []
            for t in target_order:
                if t not in dedupe:
                    dedupe.append(t)

            for _ in range(FUDGE_LOOPS):
                for t in dedupe:
                    value = choose_illegal_value(
                        rng,
                        layout,
                        len(stream),
                        start_archive,
                        start_off,
                        width,
                        t,
                        min_value=min_illegal_value,
                    )
                    if value is not None:
                        if t != illegal_target_type:
                            notes.append(f"Requested '{illegal_target_type}' not representable; used '{t}'.")
                            illegal_target_type = t
                        break
                if value is not None:
                    break
                if field_kind == "directory_name_length":
                    append_unique_dir(tree, rng, max_name_len)
                else:
                    append_unique_file(tree, rng, max_name_len, max_content_len)
                stream, fields = build_fields(tree)
                archive_count = max(1, (len(stream) + layout.logical_per_archive - 1) // layout.logical_per_archive)
                if not refresh_selected_field():
                    continue
                notes.append("Expanded tree to satisfy illegal-target mutation constraints.")
        else:
            notes.append("Target make_zero: forced integer field value to zero.")

        if value is None:
            raise RuntimeError(f"Could not derive mutation value for {case_id}")

    archive_file_len = PLAIN_HEADER_LENGTH + archive_payload_length
    if offset + width > archive_file_len:
        raise RuntimeError(
            "Selected mutation bytes exceed archive file bounds "
            f"(offset={offset}, width={width}, archive_file_len={archive_file_len})."
        )

    sorted_files = sorted(tree.files, key=lambda x: x.path)
    selected_file_record_index = -1
    recoverable_files: List[FileEntry] = []
    if field_kind in ("file_name_length", "file_content_length"):
        for idx, file_entry in enumerate(sorted_files):
            if file_entry.path == subject:
                selected_file_record_index = idx
                break
        if selected_file_record_index >= 0:
            recoverable_files = sorted_files[:selected_file_record_index]
        else:
            recoverable_files = list(sorted_files)
            notes.append("Recoverable file inference fell back to full file set (selected subject not mapped).")
    else:
        recoverable_files = list(sorted_files)

    return TestCase(
        case_id=case_id,
        size_class=size_class,
        flow=flow,
        archive_payload_length=archive_payload_length,
        l1_length=L1_LENGTH,
        field_kind=field_kind,
        illegal_target_type=illegal_target_type,
        expected_error_code=expected_error_code(field_kind, illegal_target_type),
        selected_subject=subject,
        selected_file_record_index=selected_file_record_index,
        mutation=Mutation(
            archive_index=archive_index,
            offset=offset,
            width_bytes=width,
            new_value=value,
            new_value_le_hex=le_hex(value, width),
        ),
        archive_set_mutation=archive_set_mutation,
        tree=TreeSpec(files=sorted_files, empty_dirs=effective_empty_dirs(tree)),
        recoverable_files=recoverable_files,
        failure_point_comment=make_failure_comment(field_kind, illegal_target_type),
        notes=notes,
    )


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate fence mutation test case JSON.")
    parser.add_argument("--failure-source-type", default=FAILURE_SOURCE_TYPE)
    parser.add_argument("--failure-target-type", default=FAILURE_TARGET_TYPE)
    parser.add_argument("--flow-type", default=FLOW_TYPE)
    parser.add_argument("--include-eof-source-types-in-any", action="store_true")
    parser.add_argument("--small-count", type=int, default=SMALL_COUNT)
    parser.add_argument("--medium-count", type=int, default=MEDIUM_COUNT)
    parser.add_argument("--fudge-loops", type=int, default=FUDGE_LOOPS)
    parser.add_argument("--retry-loops", type=int, default=RETRY_LOOPS)
    parser.add_argument("--random-seed", type=int, default=RANDOM_SEED)
    parser.add_argument("--output-json", default=OUTPUT_JSON)
    parser.add_argument("--archive-target-l3-blocks-lo", type=int, default=ARCHIVE_TARGET_L3_BLOCKS_LO)
    parser.add_argument("--archive-target-l3-blocks-hi", type=int, default=ARCHIVE_TARGET_L3_BLOCKS_HI)
    parser.add_argument("--max-content-length-small", type=int, default=MAX_CONTENT_LENGTH_SMALL)
    parser.add_argument("--max-content-length-medium", type=int, default=MAX_CONTENT_LENGTH_MEDIUM)
    parser.add_argument("--max-name-length-small", type=int, default=MAX_NAME_LENGTH_SMALL)
    parser.add_argument("--max-name-length-medium", type=int, default=MAX_NAME_LENGTH_MEDIUM)
    parser.add_argument("--format-constants-path", default=FORMAT_CONSTANTS_PATH)
    parser.add_argument("--sync-format-constants", action="store_true", default=SYNC_FORMAT_CONSTANTS)
    parser.add_argument("--no-sync-format-constants", action="store_false", dest="sync_format_constants")
    return parser


def apply_cli_overrides(args: argparse.Namespace) -> None:
    global FAILURE_SOURCE_TYPE
    global FAILURE_TARGET_TYPE
    global FLOW_TYPE
    global INCLUDE_EOF_SOURCE_TYPES_IN_ANY
    global SMALL_COUNT
    global MEDIUM_COUNT
    global FUDGE_LOOPS
    global RETRY_LOOPS
    global RANDOM_SEED
    global OUTPUT_JSON
    global ARCHIVE_TARGET_L3_BLOCKS_LO
    global ARCHIVE_TARGET_L3_BLOCKS_HI
    global MAX_CONTENT_LENGTH_SMALL
    global MAX_CONTENT_LENGTH_MEDIUM
    global MAX_NAME_LENGTH_SMALL
    global MAX_NAME_LENGTH_MEDIUM
    global FORMAT_CONSTANTS_PATH
    global SYNC_FORMAT_CONSTANTS

    FAILURE_SOURCE_TYPE = args.failure_source_type
    FAILURE_TARGET_TYPE = args.failure_target_type
    FLOW_TYPE = args.flow_type
    INCLUDE_EOF_SOURCE_TYPES_IN_ANY = bool(args.include_eof_source_types_in_any)
    SMALL_COUNT = max(0, args.small_count)
    MEDIUM_COUNT = max(0, args.medium_count)
    FUDGE_LOOPS = max(1, args.fudge_loops)
    RETRY_LOOPS = max(1, args.retry_loops)
    RANDOM_SEED = args.random_seed
    OUTPUT_JSON = args.output_json
    min_allowed = min(ALLOWED_ARCHIVE_BLOCK_COUNTS)
    max_allowed = max(ALLOWED_ARCHIVE_BLOCK_COUNTS)
    clamped_lo = max(min_allowed, min(max_allowed, args.archive_target_l3_blocks_lo))
    clamped_hi = max(min_allowed, min(max_allowed, args.archive_target_l3_blocks_hi))
    ARCHIVE_TARGET_L3_BLOCKS_LO = min(clamped_lo, clamped_hi)
    ARCHIVE_TARGET_L3_BLOCKS_HI = max(clamped_lo, clamped_hi)
    MAX_CONTENT_LENGTH_SMALL = max(1, args.max_content_length_small)
    MAX_CONTENT_LENGTH_MEDIUM = max(MAX_CONTENT_LENGTH_SMALL, args.max_content_length_medium)
    MAX_NAME_LENGTH_SMALL = max(1, args.max_name_length_small)
    MAX_NAME_LENGTH_MEDIUM = max(MAX_NAME_LENGTH_SMALL, args.max_name_length_medium)
    FORMAT_CONSTANTS_PATH = args.format_constants_path
    SYNC_FORMAT_CONSTANTS = bool(args.sync_format_constants)

    if SYNC_FORMAT_CONSTANTS:
        _sync_runtime_constants(FORMAT_CONSTANTS_PATH)


def main() -> None:
    parser = build_arg_parser()
    args = parser.parse_args()
    apply_cli_overrides(args)
    if SYNC_FORMAT_CONSTANTS:
        print(f"Synced constants from {FORMAT_CONSTANTS_PATH}: "
              f"header={PLAIN_HEADER_LENGTH}, recovery={RECOVERY_HEADER_LENGTH}, payload={L1_PAYLOAD_LENGTH}")

    rng = random.Random(RANDOM_SEED)
    cases: List[TestCase] = []

    def build_case(source: Optional[str], target: Optional[str], case_id: str, size_class: str) -> TestCase:
        return generate_case(case_id, size_class, rng, forced_source=source, forced_target=target)

    if FAILURE_SOURCE_TYPE == "any":
        source_pool = source_types_for_any()
    else:
        source_pool = [FAILURE_SOURCE_TYPE]

    for i in range(SMALL_COUNT):
        source = source_pool[i % len(source_pool)]
        target: Optional[str]
        if source in ("eof_gar", "eof_oob"):
            target = None
        elif FAILURE_TARGET_TYPE == "any":
            target = TARGET_TYPES[i % len(TARGET_TYPES)]
        else:
            target = FAILURE_TARGET_TYPE
        cases.append(build_case(source, target, f"CASE_SMALL_{i:03d}", "small"))

    for i in range(MEDIUM_COUNT):
        source = source_pool[i % len(source_pool)]
        target = None if source in ("eof_gar", "eof_oob") else (
            TARGET_TYPES[i % len(TARGET_TYPES)] if FAILURE_TARGET_TYPE == "any" else FAILURE_TARGET_TYPE
        )
        cases.append(build_case(source, target, f"CASE_MEDIUM_{i:03d}", "medium"))

    payload = {
        "version": 2,
        "seed": RANDOM_SEED,
        "config": {
            "failure_source_type": FAILURE_SOURCE_TYPE,
            "failure_target_type": FAILURE_TARGET_TYPE,
            "flow_type": FLOW_TYPE,
            "include_eof_source_types_in_any": INCLUDE_EOF_SOURCE_TYPES_IN_ANY,
            "small_count": SMALL_COUNT,
            "medium_count": MEDIUM_COUNT,
            "fudge_loops": FUDGE_LOOPS,
            "retry_loops": RETRY_LOOPS,
            "l1_payload_length": L1_PAYLOAD_LENGTH,
            "l1_length": L1_LENGTH,
            "sb_l3_length_default": SB_L3_LENGTH_DEFAULT,
            "archive_target_l3_blocks_lo": ARCHIVE_TARGET_L3_BLOCKS_LO,
            "archive_target_l3_blocks_hi": ARCHIVE_TARGET_L3_BLOCKS_HI,
            "allowed_archive_block_counts": ALLOWED_ARCHIVE_BLOCK_COUNTS,
            "max_content_length_small": MAX_CONTENT_LENGTH_SMALL,
            "max_content_length_medium": MAX_CONTENT_LENGTH_MEDIUM,
            "max_name_length_small": MAX_NAME_LENGTH_SMALL,
            "max_name_length_medium": MAX_NAME_LENGTH_MEDIUM,
            "plain_header_length": PLAIN_HEADER_LENGTH,
            "recovery_header_length": RECOVERY_HEADER_LENGTH,
            "format_constants_path": FORMAT_CONSTANTS_PATH,
            "sync_format_constants": SYNC_FORMAT_CONSTANTS,
        },
        "cases": [
            {
                "case_id": c.case_id,
                "size_class": c.size_class,
                "flow": c.flow,
                "archive_payload_length": c.archive_payload_length,
                "l1_length": c.l1_length,
                "field_kind": c.field_kind,
                "illegal_target_type": c.illegal_target_type,
                "expected_error_code": c.expected_error_code,
                "selected_subject": c.selected_subject,
                "selected_file_record_index": c.selected_file_record_index,
                "mutation": asdict(c.mutation),
                "tree": {
                    "files": [asdict(f) for f in c.tree.files],
                    "empty_dirs": c.tree.empty_dirs,
                },
                "recoverable_files": [asdict(f) for f in c.recoverable_files],
                "recoverable_file_count": len(c.recoverable_files),
                "failure_point_comment": c.failure_point_comment,
                "notes": c.notes,
            }
            for c in cases
        ],
    }

    output_path = Path(OUTPUT_JSON)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")
    print(f"Wrote {len(cases)} generated cases to {output_path}")


if __name__ == "__main__":
    main()
