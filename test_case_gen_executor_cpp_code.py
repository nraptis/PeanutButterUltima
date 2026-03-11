#!/usr/bin/env python3
"""
Generate runnable C++ fence tests from JSON cases with inline case data.

Examples:
  python3 test_case_gen_executor_cpp_code.py
  python3 test_case_gen_executor_cpp_code.py --input tests/generated/test_cases_all.json
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any, Dict, List


DEFAULT_INPUT_JSON = "tests/generated/test_cases.json"
DEFAULT_OUTPUT_DIR = "tests/generated"
DEFAULT_ALLOWED_BLOCK_COUNTS = (1, 2, 3, 4)


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
    for file_entry in files:
        if not isinstance(file_entry, dict):
            continue
        out.append(
            {
                "path": str(file_entry.get("path", "")),
                "content": str(file_entry.get("content", "")),
            }
        )
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


def format_string(value: str) -> str:
    return f'"{cxx_escape(value)}"'


def format_string_vector(values: List[str]) -> str:
    if not values:
        return "{}"
    return "{ " + ", ".join(format_string(v) for v in values) + " }"


def format_seed_files(files: List[Dict[str, str]]) -> str:
    if not files:
        return "{}"
    chunks: List[str] = []
    for file_entry in files:
        chunks.append(
            "        {"
            + f"{format_string(file_entry['path'])}, {format_string(file_entry['content'])}"
            + "}"
        )
    return "{\n" + ",\n".join(chunks) + "\n      }"


def format_bytes(values: List[int]) -> str:
    if not values:
        return "{}"
    return "{ " + ", ".join(f"0x{value:02x}" for value in values) + " }"


def format_u32_vector(values: List[int]) -> str:
    if not values:
        return "{}"
    return "{ " + ", ".join(f"static_cast<std::uint32_t>({value}u)" for value in values) + " }"


def format_case_initializer(record: Dict[str, Any]) -> str:
    fields = [
        format_string(record["case_id"]),
        format_string(record["flow"]),
        format_string(record["field_kind"]),
        format_string(record["expected_error_code"]),
        format_string(record["expected_fence_flag"]),
        format_string_vector(record["forbidden_fence_flags"]),
        f"static_cast<std::size_t>({record['archive_block_count']}u)",
        format_seed_files(record["input_files"]),
        format_string_vector(record["input_empty_dirs"]),
        format_seed_files(record["recoverable_files"]),
        f"static_cast<std::uint32_t>({record['archive_index']}u)",
        f"static_cast<std::size_t>({record['mutation_file_offset']}u)",
        f"static_cast<std::int64_t>({record['mutation_payload_logical_offset']})",
        format_bytes(record["mutation_bytes"]),
        format_u32_vector(record["create_archive_indices"]),
        format_u32_vector(record["remove_archive_indices"]),
        format_string(record["failure_point_comment"]),
    ]
    return "    {\n      " + ",\n      ".join(fields) + "\n    }"


def emit_cpp_for_group(source_name: str, file_name: str, records: List[Dict[str, Any]]) -> str:
    payload_guard = ""
    if "EndOfFile_TrailingGarbage" in file_name:
        payload_guard = """\
  if (peanutbutter::SB_PAYLOAD_SIZE <= (peanutbutter::SB_RECOVERY_HEADER_LENGTH + 1)) {
    std::cout << "[WARN] generated fence suite invalid L1 block size for " << kCases.size() << " case(s).\\n";
    return 0;
  }

"""
    initializers = ",\n".join(format_case_initializer(record) for record in records)
    return f"""// Generated by test_case_gen_executor_cpp_code.py from {cxx_escape(source_name)}
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "FormatConstants.hpp"
#include "Test_Execute_FenceRoundTrip.hpp"
#include "Test_GeneratedFenceCaseData.hpp"
#include "Test_Utils.hpp"

namespace {{

using peanutbutter::testing::FenceRoundTripOutcome;
using peanutbutter::testing::FenceRoundTripSpec;
using peanutbutter::testing::GeneratedFenceCaseData;
using peanutbutter::testing::GenericArchiveDataMutation;
using peanutbutter::testing::GenericArchiveByteMutation;

const std::vector<GeneratedFenceCaseData> kCases = {{
{initializers}
}};

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
{payload_guard}\
  for (const GeneratedFenceCaseData& aCase : kCases) {{
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

  std::cout << "[PASS] generated fence suite passed for " << kCases.size() << " case(s).\\n";
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
    parser = argparse.ArgumentParser(description="Generate runnable C++ fence tests from JSON with inline case data.")
    parser.add_argument("--input", default=DEFAULT_INPUT_JSON, help="Input JSON case file.")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, help="Directory for generated .cpp tests.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    data = json.loads(input_path.read_text(encoding="utf-8"))
    cases = list(data.get("cases", []))
    config = dict(data.get("config", {}))
    grouped = group_cases_by_output_file(cases)

    legacy_paths = [
        output_dir / "Test_GeneratedMutationCases_Auto.cpp",
    ]
    legacy_paths.extend(output_dir.glob("Test_Fences_*_Generated.cpp"))
    for legacy in legacy_paths:
        if legacy.exists():
            legacy.unlink()

    written_cpp: List[str] = []
    written_cpp_set = set()

    for file_name, group_cases in sorted(grouped.items()):
        records = [case_to_record(case, config) for case in group_cases]
        cpp_source = emit_cpp_for_group(input_path.name, file_name, records)
        cpp_path = output_dir / file_name
        cpp_path.write_text(cpp_source, encoding="utf-8")

        written_cpp.append(str(cpp_path))
        written_cpp_set.add(cpp_path.resolve())

    for existing in output_dir.glob("*.cpp"):
        if existing.resolve() in written_cpp_set:
            continue
        try:
            head = existing.read_text(encoding="utf-8").splitlines()[0]
        except Exception:
            continue
        if head.startswith("// Generated by test_case_gen_executor_cpp_"):
            existing.unlink()

    print(f"Wrote {len(written_cpp)} generated C++ test file(s) from {input_path}:")
    for path in written_cpp:
        print(f"  - {path}")


if __name__ == "__main__":
    main()
