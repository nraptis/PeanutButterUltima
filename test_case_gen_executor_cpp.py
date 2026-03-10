#!/usr/bin/env python3
"""
Generate runnable C++ fence tests from JSON cases.

Examples:
  python3 test_case_gen_executor_cpp.py
  python3 test_case_gen_executor_cpp.py --input tests/generated/test_cases_all.json
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, List, Tuple


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


def emit_seed_files(files: List[Dict[str, str]], indent: str) -> str:
    if not files:
        return "{}"
    rows = []
    for f in files:
        path = cxx_escape(str(f.get("path", "")))
        content = cxx_escape(str(f.get("content", "")))
        rows.append(f'{indent}{{"{path}", "{content}"}}')
    return "{\n" + ",\n".join(rows) + "\n" + indent[:-2] + "}"


def emit_string_list(items: List[str], indent: str) -> str:
    if not items:
        return "{}"
    rows = [f'{indent}"{cxx_escape(item)}"' for item in items]
    return "{\n" + ",\n".join(rows) + "\n" + indent[:-2] + "}"


def emit_byte_vector(bytes_list: List[int], indent: str) -> str:
    if not bytes_list:
        return "{}"
    rows = ", ".join(f"0x{b:02X}" for b in bytes_list)
    return "{ " + rows + " }"


def emit_u32_vector(values: List[int]) -> str:
    if not values:
        return "{}"
    rows = ", ".join(f"static_cast<std::uint32_t>({int(v)}u)" for v in values)
    return "{ " + rows + " }"


def case_to_cpp_initializer(case: Dict[str, Any], config: Dict[str, Any]) -> str:
    case_id = cxx_escape(str(case.get("case_id", "")))
    flow = cxx_escape(str(case.get("flow", "unbundle")))
    field_kind = cxx_escape(str(case.get("field_kind", "")))
    expected_error = cxx_escape(str(case.get("expected_error_code", "UNK_SYS_001")))
    expected_flag = cxx_escape(expected_fence_flag(case))
    forbidden_flags = forbidden_fence_flags(case)
    comment = cxx_escape(str(case.get("failure_point_comment", "")))
    blocks = archive_block_count(case, config)

    tree_files = list(case.get("tree", {}).get("files", []))
    tree_empty_dirs = [str(v) for v in list(case.get("tree", {}).get("empty_dirs", []))]
    recoverable_files = infer_recoverable_files(case)
    mutation = dict(case.get("mutation", {}))
    archive_index = int(mutation.get("archive_index", 0))
    file_offset = int(mutation.get("offset", 0))
    mutation_bytes = bytes_from_hex(str(mutation.get("new_value_le_hex", "")))
    archive_set_mut = dict(case.get("archive_set_mutation", {}))
    create_indices = [int(v) for v in list(archive_set_mut.get("create_archive_indices", []))]
    remove_indices = [int(v) for v in list(archive_set_mut.get("remove_archive_indices", []))]

    input_init = emit_seed_files(tree_files, "      ")
    empty_dirs_init = emit_string_list(tree_empty_dirs, "      ")
    recoverable_init = emit_seed_files(recoverable_files, "      ")
    bytes_init = emit_byte_vector(mutation_bytes, "      ")
    create_init = emit_u32_vector(create_indices)
    remove_init = emit_u32_vector(remove_indices)
    forbidden_flags_init = emit_string_list(forbidden_flags, "      ")

    return (
        "    {\n"
        f'      "{case_id}",\n'
        f'      "{flow}",\n'
        f'      "{field_kind}",\n'
        f'      "{expected_error}",\n'
        f'      "{expected_flag}",\n'
        f"      {forbidden_flags_init},\n"
        f"      {blocks}u,\n"
        f"      {input_init},\n"
        f"      {empty_dirs_init},\n"
        f"      {recoverable_init},\n"
        f"      static_cast<std::uint32_t>({archive_index}u),\n"
        f"      static_cast<std::size_t>({file_offset}u),\n"
        f"      {bytes_init},\n"
        f"      {create_init},\n"
        f"      {remove_init},\n"
        f'      "{comment}"\n'
        "    }"
    )


def emit_cpp_for_group(cases: List[Dict[str, Any]], config: Dict[str, Any], source_name: str) -> str:
    rows = ",\n".join(case_to_cpp_initializer(c, config) for c in cases)
    return f"""// Generated by test_case_gen_executor_cpp.py from {cxx_escape(source_name)}
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Test_Execute_FenceRoundTrip.hpp"
#include "Test_Utils.hpp"

namespace {{

using peanutbutter::testing::FenceRoundTripOutcome;
using peanutbutter::testing::FenceRoundTripSpec;
using peanutbutter::testing::GenericArchiveByteMutation;
using peanutbutter::testing::TestSeedFile;

struct GeneratedFenceCase {{
  std::string mCaseId;
  std::string mFlow;
  std::string mFieldKind;
  std::string mExpectedErrorCode;
  std::string mExpectedFenceFlag;
  std::vector<std::string> mForbiddenFenceFlags;
  std::size_t mArchiveBlockCount = 1;
  std::vector<TestSeedFile> mInputFiles;
  std::vector<std::string> mInputEmptyDirs;
  std::vector<TestSeedFile> mRecoverableFiles;
  std::uint32_t mArchiveIndex = 0;
  std::size_t mMutationFileOffset = 0;
  std::vector<unsigned char> mMutationBytes;
  std::vector<std::uint32_t> mCreateArchiveIndices;
  std::vector<std::uint32_t> mRemoveArchiveIndices;
  std::string mFailurePointComment;
}};

const std::vector<GeneratedFenceCase> kCases = {{
{rows}
}};

bool ValidateCaseOutcome(const GeneratedFenceCase& pCase,
                         const FenceRoundTripOutcome& pOutcome,
                         std::string& pError) {{
  if (!pOutcome.mSucceeded) {{
    pError = pOutcome.mFailureMessage;
    return false;
  }}

  const bool aHasExpectedError =
      peanutbutter::testing::ContainsToken(pOutcome.mMutatedUnbundleMessage, pCase.mExpectedErrorCode) ||
      peanutbutter::testing::ContainsToken(pOutcome.mMutatedRecoverMessage, pCase.mExpectedErrorCode) ||
      peanutbutter::testing::ContainsToken(pOutcome.mCollectedLogs, pCase.mExpectedErrorCode);
  if (!aHasExpectedError) {{
    const bool aHasGenericFailure =
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedUnbundleMessage, "failed") ||
        peanutbutter::testing::ContainsToken(pOutcome.mMutatedRecoverMessage, "failed") ||
        peanutbutter::testing::ContainsToken(pOutcome.mCollectedLogs, "[error]");
    if (!aHasGenericFailure) {{
      pError = "missing expected error code and generic failure signal: " + pCase.mExpectedErrorCode;
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
  for (const GeneratedFenceCase& aCase : kCases) {{
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

    GenericArchiveByteMutation aMutation;
    aMutation.mArchiveIndex = aCase.mArchiveIndex;
    aMutation.mFileOffset = aCase.mMutationFileOffset;
    aMutation.mBytes = aCase.mMutationBytes;
    aSpec.mMutation.mArchiveByteMutations.push_back(aMutation);
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
    parser = argparse.ArgumentParser(description="Generate runnable C++ fence tests from JSON.")
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

    # Legacy cleanup from older non-runnable generator output.
    legacy_paths = [
        output_dir / "Test_GeneratedMutationCases_Auto.cpp",
    ]
    legacy_paths.extend(output_dir.glob("Test_Fences_*_Generated.cpp"))
    for legacy in legacy_paths:
        if legacy.exists():
            legacy.unlink()

    written: List[str] = []
    written_set = set()
    for file_name, group_cases in sorted(grouped.items()):
        cpp_source = emit_cpp_for_group(group_cases, config, input_path.name)
        output_path = output_dir / file_name
        output_path.write_text(cpp_source, encoding="utf-8")
        written.append(str(output_path))
        written_set.add(output_path.resolve())

    # Remove stale generated files produced by previous runs of this script.
    for existing in output_dir.glob("*.cpp"):
        if existing.resolve() in written_set:
            continue
        try:
            head = existing.read_text(encoding="utf-8").splitlines()[0]
        except Exception:
            continue
        if head.startswith("// Generated by test_case_gen_executor_cpp.py"):
            existing.unlink()

    print(f"Wrote {len(written)} generated C++ test file(s) from {input_path}:")
    for path in written:
        print(f"  - {path}")


if __name__ == "__main__":
    main()
