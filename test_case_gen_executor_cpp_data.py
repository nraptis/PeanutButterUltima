#!/usr/bin/env python3
"""
Generate runnable C++ fence tests from JSON cases with external .dat data files.

Examples:
  python3 test_case_gen_executor_cpp_data.py
  python3 test_case_gen_executor_cpp_data.py --input tests/generated/test_cases_all.json
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any, BinaryIO, Dict, List, Tuple


DEFAULT_INPUT_JSON = "tests/generated/test_cases.json"
DEFAULT_OUTPUT_DIR = "tests/generated"
DEFAULT_DATA_DIR_SUFFIX = "data"
DEFAULT_ALLOWED_BLOCK_COUNTS = (1, 2, 3, 4)
CASE_DATA_MAGIC = b"PBFCASE1"
CASE_DATA_VERSION = 1


def cxx_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def bytes_from_hex(hex_text: str) -> List[int]:
    text = (hex_text or "").strip()
    if len(text) % 2 != 0:
        raise ValueError(f"Expected even-length hex text, got: {hex_text!r}")
    out: List[int] = []
    for i in range(0, len(text), 2):
        out.append(int(text[i : i + 2], 16))
    return out


def source_stem(source: str) -> str:
    mapping = {
        "file_name_length": "FileName",
        "file_content_length": "FileContent",
        "directory_name_length": "FolderName",
        "recovery_header": "RecoveryStride",
        "eof_gar": "EndOfFile",
        "eof_oob": "EndOfFile",
    }
    return mapping.get(source, "Unknown")


def derive_pretty_case_name(case: Dict[str, Any]) -> str:
    source = str(case.get("field_kind", ""))
    target = str(case.get("illegal_target_type", ""))

    if source == "eof_gar":
        return "EndOfFile_TrailingGarbage"
    if source == "eof_oob":
        return "EndOfFile_TrailingArchives"

    if target == "make_zero":
        if source == "file_name_length":
            return "FileName_Zero_MidPayload"
        if source == "directory_name_length":
            return "FolderName_Zero_MidPayload"
        if source == "file_content_length":
            return "FileContent_Zero_Desync"
        if source == "recovery_header":
            return "RecoveryStride_Zero"

    suffix = {
        "out_of_entire_archive_list_bounds": "MissingArchive",
        "out_of_this_archive_bounds": "Outlying",
        "within_recovery_header": "InRecoveryHeader",
        "within_archive_header": "InArchiveHeader",
    }.get(target, "Unknown")
    return f"{source_stem(source)}_{suffix}"


def derive_output_cpp_file(case: Dict[str, Any]) -> str:
    pretty = case.get("pretty_cpp_file_name")
    if isinstance(pretty, str) and pretty.strip():
        return pretty.strip()
    return f"Test_Fences_{derive_pretty_case_name(case)}.cpp"


def sanitize_cpp_stem(stem: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", stem)


def derive_suite_target_name(file_name: str) -> str:
    return f"PeanutButterUltimaGenerated_{sanitize_cpp_stem(Path(file_name).stem)}"


def derive_data_file_name(file_name: str) -> str:
    return f"{derive_suite_target_name(file_name)}.dat"


def expected_fence_flag(case: Dict[str, Any]) -> str:
    value = case.get("expected_fence_flag", "")
    return str(value) if isinstance(value, str) else ""


def forbidden_fence_flags(case: Dict[str, Any]) -> List[str]:
    values = case.get("forbidden_fence_flags", [])
    if not isinstance(values, list):
        return []
    out: List[str] = []
    for value in values:
        if isinstance(value, str) and value:
            out.append(value)
    return out


def infer_recoverable_files(case: Dict[str, Any]) -> List[Dict[str, str]]:
    recoverable = case.get("recoverable_files")
    if isinstance(recoverable, list):
        return recoverable

    tree_files = list(case.get("tree", {}).get("files", []))
    idx = case.get("selected_file_record_index")
    if isinstance(idx, int) and idx >= 0:
        return tree_files[:idx]
    return tree_files


def archive_block_count(case: Dict[str, Any], config: Dict[str, Any]) -> int:
    allowed_values = config.get("allowed_archive_block_counts", DEFAULT_ALLOWED_BLOCK_COUNTS)
    try:
        allowed = {int(v) for v in allowed_values}
    except Exception as exc:
        raise ValueError("Invalid allowed_archive_block_counts in config.") from exc
    if not allowed:
        raise ValueError("allowed_archive_block_counts must not be empty.")

    explicit = case.get("archive_block_count")
    if explicit is not None:
        value = int(explicit)
        if value not in allowed:
            raise ValueError(f"archive_block_count {value} is not in allowed set {sorted(allowed)}")
        return value

    payload = int(case.get("archive_payload_length", 0))
    sb_l3 = int(config.get("sb_l3_length_default", 0))
    if payload > 0 and sb_l3 > 0 and (payload % sb_l3) == 0:
        value = payload // sb_l3
        if value not in allowed:
            raise ValueError(
                f"Derived archive_block_count {value} not in allowed set {sorted(allowed)} "
                f"(payload={payload}, sb_l3={sb_l3})."
            )
        return value

    raise ValueError(
        "Could not derive archive_block_count: missing valid archive_block_count and "
        "archive_payload_length is not a multiple of sb_l3_length_default."
    )


def normalize_seed_files(files: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    out: List[Dict[str, str]] = []
    for f in files:
        if not isinstance(f, dict):
            continue
        out.append({
            "path": str(f.get("path", "")),
            "content": str(f.get("content", "")),
        })
    return out


def case_to_record(case: Dict[str, Any], config: Dict[str, Any]) -> Dict[str, Any]:
    mutation = dict(case.get("mutation", {}))
    archive_set_mut = dict(case.get("archive_set_mutation", {}))
    tree = dict(case.get("tree", {}))

    return {
        "case_id": str(case.get("case_id", "")),
        "flow": str(case.get("flow", "unbundle")),
        "field_kind": str(case.get("field_kind", "")),
        "expected_error_code": str(case.get("expected_error_code", "UNK_SYS_001")),
        "expected_fence_flag": expected_fence_flag(case),
        "forbidden_fence_flags": forbidden_fence_flags(case),
        "archive_block_count": int(archive_block_count(case, config)),
        "input_files": normalize_seed_files(list(tree.get("files", []))),
        "input_empty_dirs": [str(v) for v in list(tree.get("empty_dirs", []))],
        "recoverable_files": normalize_seed_files(infer_recoverable_files(case)),
        "archive_index": int(mutation.get("archive_index", 0)),
        "mutation_file_offset": int(mutation.get("offset", 0)),
        "mutation_payload_logical_offset": int(mutation.get("payload_logical_offset", -1)),
        "mutation_bytes": bytes_from_hex(str(mutation.get("new_value_le_hex", ""))),
        "create_archive_indices": [int(v) for v in list(archive_set_mut.get("create_archive_indices", []))],
        "remove_archive_indices": [int(v) for v in list(archive_set_mut.get("remove_archive_indices", []))],
        "failure_point_comment": str(case.get("failure_point_comment", "")),
    }


def write_u32(stream: BinaryIO, value: int) -> None:
    stream.write(int(value).to_bytes(4, byteorder="little", signed=False))


def write_u64(stream: BinaryIO, value: int) -> None:
    stream.write(int(value).to_bytes(8, byteorder="little", signed=False))


def write_i64(stream: BinaryIO, value: int) -> None:
    stream.write(int(value).to_bytes(8, byteorder="little", signed=True))


def write_string(stream: BinaryIO, value: str) -> None:
    payload = value.encode("utf-8")
    write_u32(stream, len(payload))
    stream.write(payload)


def write_string_list(stream: BinaryIO, values: List[str]) -> None:
    write_u32(stream, len(values))
    for value in values:
        write_string(stream, value)


def write_seed_files(stream: BinaryIO, files: List[Dict[str, str]]) -> None:
    write_u32(stream, len(files))
    for file_entry in files:
        write_string(stream, file_entry["path"])
        write_string(stream, file_entry["content"])


def write_u32_list(stream: BinaryIO, values: List[int]) -> None:
    write_u32(stream, len(values))
    for value in values:
        write_u32(stream, value)


def write_bytes(stream: BinaryIO, values: List[int]) -> None:
    write_u32(stream, len(values))
    stream.write(bytes(values))


def write_case_data_file(output_path: Path, cases: List[Dict[str, Any]], config: Dict[str, Any]) -> int:
    records = [case_to_record(case, config) for case in cases]
    with output_path.open("wb") as fh:
        fh.write(CASE_DATA_MAGIC)
        write_u32(fh, CASE_DATA_VERSION)
        write_u32(fh, len(records))
        for record in records:
            write_string(fh, record["case_id"])
            write_string(fh, record["flow"])
            write_string(fh, record["field_kind"])
            write_string(fh, record["expected_error_code"])
            write_string(fh, record["expected_fence_flag"])
            write_string_list(fh, record["forbidden_fence_flags"])
            write_u32(fh, record["archive_block_count"])
            write_seed_files(fh, record["input_files"])
            write_string_list(fh, record["input_empty_dirs"])
            write_seed_files(fh, record["recoverable_files"])
            write_u32(fh, record["archive_index"])
            write_u64(fh, record["mutation_file_offset"])
            write_i64(fh, record["mutation_payload_logical_offset"])
            write_bytes(fh, record["mutation_bytes"])
            write_u32_list(fh, record["create_archive_indices"])
            write_u32_list(fh, record["remove_archive_indices"])
            write_string(fh, record["failure_point_comment"])
    return len(records)


def emit_cpp_for_group(source_name: str, data_file_name: str) -> str:
    payload_guard = ""
    if "EndOfFile_TrailingGarbage" in data_file_name:
        payload_guard = """\
  if (peanutbutter::SB_PAYLOAD_SIZE <= (peanutbutter::SB_RECOVERY_HEADER_LENGTH + 1)) {
    std::cout << "[WARN] generated fence suite invalid L1 block size for " << aCases.size() << " case(s).\\n";
    return 0;
  }

"""
    return f"""// Generated by test_case_gen_executor_cpp_data.py from {cxx_escape(source_name)}
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "FormatConstants.hpp"
#include "Test_Execute_FenceRoundTrip.hpp"
#include "Test_GeneratedFenceCaseData.hpp"
#include "Test_Utils.hpp"

#ifndef PEANUT_BUTTER_ULTIMA_TEST_DATA_DIR
#define PEANUT_BUTTER_ULTIMA_TEST_DATA_DIR "."
#endif

namespace {{

using peanutbutter::testing::FenceRoundTripOutcome;
using peanutbutter::testing::FenceRoundTripSpec;
using peanutbutter::testing::GeneratedFenceCaseData;
using peanutbutter::testing::GenericArchiveDataMutation;
using peanutbutter::testing::GenericArchiveByteMutation;

bool ValidateCaseOutcome(const GeneratedFenceCaseData& pCase,
                         const FenceRoundTripOutcome& pOutcome,
                         std::string& pError) {{
  if (!pOutcome.mSucceeded) {{
    pError = pOutcome.mFailureMessage;
    return false;
  }}

  if (!pCase.mExpectedErrorCode.empty()) {{
    const bool aHasExpectedError =
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedUnbundleMessage, pCase.mExpectedErrorCode) ||
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedRecoverMessage, pCase.mExpectedErrorCode) ||
        peanutbutter::testing::ContainsToken(pOutcome.mCollectedLogs, pCase.mExpectedErrorCode);
    if (!aHasExpectedError) {{
      pError = "missing expected error code: " + pCase.mExpectedErrorCode;
      return false;
    }}
  }}

  if (!pCase.mExpectedFenceFlag.empty()) {{
    const bool aHasFenceFlag =
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedUnbundleMessage, pCase.mExpectedFenceFlag) ||
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedRecoverMessage, pCase.mExpectedFenceFlag) ||
        peanutbutter::testing::ContainsToken(pOutcome.mCollectedLogs, pCase.mExpectedFenceFlag);
    if (!aHasFenceFlag) {{
      pError = "missing expected fence flag: " + pCase.mExpectedFenceFlag;
      return false;
    }}
  }}

  for (const std::string& aForbiddenFlag : pCase.mForbiddenFenceFlags) {{
    const bool aHasForbiddenFlag =
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedUnbundleMessage, aForbiddenFlag) ||
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedRecoverMessage, aForbiddenFlag) ||
        peanutbutter::testing::ContainsToken(pOutcome.mCollectedLogs, aForbiddenFlag);
    if (aHasForbiddenFlag) {{
      pError = "encountered forbidden fence flag: " + aForbiddenFlag;
      return false;
    }}
  }}

  return true;
}}

}}  // namespace

int main() {{
  const std::string aCaseDataPath = std::string(PEANUT_BUTTER_ULTIMA_TEST_DATA_DIR) + "/{cxx_escape(data_file_name)}";

  std::vector<GeneratedFenceCaseData> aCases;
  std::string aLoadError;
  if (!peanutbutter::testing::LoadGeneratedFenceCaseDataFile(aCaseDataPath, aCases, aLoadError)) {{
    std::cerr << "[FAIL] could not load generated fence case data: " << aLoadError << "\\n";
    return 1;
  }}

{payload_guard}\
  for (const GeneratedFenceCaseData& aCase : aCases) {{
    FenceRoundTripSpec aSpec;
    aSpec.mCaseName = aCase.mCaseId;
    aSpec.mOriginalFiles = aCase.mInputFiles;
    aSpec.mOriginalEmptyDirectories = aCase.mInputEmptyDirs;
    aSpec.mRecoverableFiles = aCase.mRecoverableFiles;
    aSpec.mArchiveBlockCount = aCase.mArchiveBlockCount;
    aSpec.mExpectMutatedUnbundleFailure = (aCase.mFlow == "unbundle");
    aSpec.mExpectedUnbundleErrorCode = (aCase.mFlow == "unbundle") ? aCase.mExpectedErrorCode : "";
    aSpec.mRunRecoverAfterMutation = (aCase.mFlow == "recover");
    aSpec.mRequireRecoverTreeMatch = false;

    const bool aSupportsPayloadMutation =
        (aCase.mFieldKind == "file_name_length") ||
        (aCase.mFieldKind == "file_content_length") ||
        (aCase.mFieldKind == "directory_name_length") ||
        (aCase.mFieldKind == "eof_gar");
    if (aSupportsPayloadMutation && aCase.mMutationPayloadLogicalOffset >= 0) {{
      GenericArchiveDataMutation aMutation;
      aMutation.mArchiveIndex = aCase.mArchiveIndex;
      aMutation.mPayloadLogicalOffset = static_cast<std::size_t>(aCase.mMutationPayloadLogicalOffset);
      aMutation.mBytes = aCase.mMutationBytes;
      aSpec.mMutation.mArchiveDataMutations.push_back(aMutation);
    }} else {{
      GenericArchiveByteMutation aMutation;
      aMutation.mArchiveIndex = aCase.mArchiveIndex;
      aMutation.mFileOffset = aCase.mMutationFileOffset;
      aMutation.mBytes = aCase.mMutationBytes;
      aSpec.mMutation.mArchiveByteMutations.push_back(aMutation);
    }}
    aSpec.mMutation.mArchiveSetMutation.mCreateArchiveIndices = aCase.mCreateArchiveIndices;
    aSpec.mMutation.mArchiveSetMutation.mRemoveArchiveIndices = aCase.mRemoveArchiveIndices;

    const FenceRoundTripOutcome aOutcome = peanutbutter::testing::ExecuteFenceTestRoundTrip(aSpec);
    std::string aError;
    if (!ValidateCaseOutcome(aCase, aOutcome, aError)) {{
      std::cerr << "[FAIL] " << aCase.mCaseId << " failed: " << aError << "\\n";
      if (!aCase.mFailurePointComment.empty()) {{
        std::cerr << "[INFO] " << aCase.mFailurePointComment << "\\n";
      }}
      std::cerr << aOutcome.mCollectedLogs;
      return 1;
    }}
  }}

  std::cout << "[PASS] generated fence suite passed for " << aCases.size() << " case(s).\\n";
  return 0;
}}
"""


def group_cases_by_output_file(cases: List[Dict[str, Any]]) -> Dict[str, List[Dict[str, Any]]]:
    grouped: Dict[str, List[Dict[str, Any]]] = {}
    for case in cases:
        file_name = derive_output_cpp_file(case)
        grouped.setdefault(file_name, []).append(case)
    return grouped


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate runnable C++ fence tests from JSON.")
    parser.add_argument("--input", default=DEFAULT_INPUT_JSON, help="Input JSON case file.")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, help="Directory for generated .cpp tests.")
    parser.add_argument(
        "--data-dir",
        default="",
        help="Directory for generated .dat case files (default: <output-dir>/data).",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    data_dir = Path(args.data_dir) if args.data_dir else (output_dir / DEFAULT_DATA_DIR_SUFFIX)
    data_dir.mkdir(parents=True, exist_ok=True)

    data = json.loads(input_path.read_text(encoding="utf-8"))
    cases = list(data.get("cases", []))
    config = dict(data.get("config", {}))
    grouped = group_cases_by_output_file(cases)

    # Legacy cleanup from older non-runnable generator output.
    legacy_paths = [
        output_dir / "Test_GeneratedMutationCases_Auto.cpp",
    ]
    legacy_paths.extend(output_dir.glob("Test_Fences_*_Generated.cpp"))
    for legacy in legacy_paths:
        if legacy.exists():
            legacy.unlink()

    written_cpp: List[str] = []
    written_cpp_set = set()
    written_data: List[str] = []
    written_data_set = set()

    for file_name, group_cases in sorted(grouped.items()):
        data_file_name = derive_data_file_name(file_name)
        data_path = data_dir / data_file_name
        case_count = write_case_data_file(data_path, group_cases, config)

        cpp_source = emit_cpp_for_group(input_path.name, data_file_name)
        cpp_path = output_dir / file_name
        cpp_path.write_text(cpp_source, encoding="utf-8")

        written_cpp.append(str(cpp_path))
        written_cpp_set.add(cpp_path.resolve())
        written_data.append(f"{data_path} ({case_count} case(s))")
        written_data_set.add(data_path.resolve())

    # Remove stale generated .cpp files produced by previous runs of this script.
    for existing in output_dir.glob("*.cpp"):
        if existing.resolve() in written_cpp_set:
            continue
        try:
            head = existing.read_text(encoding="utf-8").splitlines()[0]
        except Exception:
            continue
        if head.startswith("// Generated by test_case_gen_executor_cpp_"):
            existing.unlink()

    # Remove stale generated .dat files from this script.
    for existing in data_dir.glob("*.dat"):
        if existing.resolve() in written_data_set:
            continue
        if existing.name.startswith("PeanutButterUltimaGenerated_"):
            existing.unlink()

    print(f"Wrote {len(written_cpp)} generated C++ test file(s) from {input_path}:")
    for path in written_cpp:
        print(f"  - {path}")
    print(f"Wrote {len(written_data)} generated data file(s) in {data_dir}:")
    for path in written_data:
        print(f"  - {path}")


if __name__ == "__main__":
    main()
