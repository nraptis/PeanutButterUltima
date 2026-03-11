#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from triage_generated_failures import parse_test_execution_l1_payload_length


@dataclass
class BrewRunResult:
    failed_tests_raw: int
    failed_tests_effective: int
    total_tests_effective: int
    ignored_failed_tests: list[str]
    ignored_present_tests: list[str]
    non_ignored_failed_tests: list[str]
    non_ignored_passed_tests: list[str]
    generated_total: int
    generated_failed: int
    generated_passed: list[str]
    generated_failed_tests: list[str]
    total_tests: int
    return_code: int
    raw_output: str


def set_test_payload_size_n(format_constants_path: Path, n: int) -> None:
    text = format_constants_path.read_text(encoding="utf-8")
    block_match = re.search(
        r"(#ifdef\s+PEANUT_BUTTER_ULTIMA_TEST_BUILD)(.*?)(#else)",
        text,
        flags=re.DOTALL,
    )
    if not block_match:
        raise RuntimeError("Could not locate PEANUT_BUTTER_ULTIMA_TEST_BUILD block in FormatConstants.hpp")

    head = block_match.group(1)
    block = block_match.group(2)
    tail_marker = block_match.group(3)

    new_line = f"inline constexpr std::size_t SB_PAYLOAD_SIZE = SB_RECOVERY_HEADER_LENGTH + {n};"
    replaced_block, replacements = re.subn(
        r"^[ \t]*inline\s+constexpr\s+std::size_t\s+SB_PAYLOAD_SIZE\s*=\s*[^;]+;[ \t]*$",
        new_line,
        block,
        count=1,
        flags=re.MULTILINE,
    )
    if replacements != 1:
        raise RuntimeError("Could not replace SB_PAYLOAD_SIZE assignment in test-build block.")

    updated = text[: block_match.start()] + head + replaced_block + tail_marker + text[block_match.end() :]
    format_constants_path.write_text(updated, encoding="utf-8")


def run_command(cmd: list[str], cwd: Path, *, allowed_return_codes: set[int] | None = None) -> tuple[str, int]:
    if allowed_return_codes is None:
        allowed_return_codes = {0}
    proc = subprocess.run(
        cmd,
        cwd=str(cwd),
        capture_output=True,
        text=True,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    print(output, end="")
    if proc.returncode not in allowed_return_codes:
        raise RuntimeError(f"Command failed ({proc.returncode}): {' '.join(cmd)}")
    return output, proc.returncode


def parse_generation_l1_payload(cases_path: Path) -> int | None:
    if not cases_path.exists():
        return None
    try:
        data = json.loads(cases_path.read_text(encoding="utf-8"))
        raw = (data.get("config") or {}).get("l1_payload_length")
        if raw is None:
            return None
        return int(raw)
    except Exception:
        return None


def parse_failed_test_names(output: str) -> list[str]:
    section = re.search(r"FAILED TESTS:\n((?:\s+-[^\n]*\n?)*)", output)
    if not section:
        return []
    names: list[str] = []
    for line in section.group(1).splitlines():
        line = line.strip()
        if line.startswith("- "):
            names.append(line[2:].strip())
    return names


def parse_discovered_test_names(cwd: Path) -> list[str]:
    test_list = cwd / "build-test-release" / "ctest_brew_list.txt"
    if not test_list.exists():
        return []
    names: list[str] = []
    for line in test_list.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = re.match(r"^\s*Test\s*#\d+:\s*(.+?)\s*$", line)
        if match:
            names.append(match.group(1))
    return names


def parse_recovery_header_length(format_constants_path: Path) -> int:
    text = format_constants_path.read_text(encoding="utf-8")
    checksum_match = re.search(r"SB_RECOVERY_CHECKSUM_LENGTH\s*=\s*([0-9]+)\s*;", text)
    stride_match = re.search(r"SB_RECOVERY_STRIDE_LENGTH\s*=\s*([0-9]+)\s*;", text)
    if not checksum_match or not stride_match:
        raise RuntimeError("Could not parse recovery header constants from FormatConstants.hpp")
    return int(checksum_match.group(1)) + int(stride_match.group(1))


def run_brew_release_and_parse(
    cwd: Path,
    ignored_failed_tests: set[str],
    ignored_test_prefixes: tuple[str, ...] = (),
) -> BrewRunResult:
    output, return_code = run_command([str(cwd / "run_brew_tests_release.sh")], cwd, allowed_return_codes={0, 1, 2, 8})
    match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", output)
    if not match:
        if "RESULT: BUILD CONFIGURE FAILED" in output:
            raise RuntimeError(
                "Release test run failed during CMake configure. "
                "See run_brew_tests_detailed_logs.txt for compiler/configuration details."
            )
        if "RESULT: BUILD FAILED" in output:
            raise RuntimeError(
                "Release test run failed during build. "
                "See run_brew_tests_detailed_logs.txt for compiler details."
            )
        if "CTest discovery failed" in output:
            raise RuntimeError(
                "Release test run failed during CTest discovery. "
                "See run_brew_tests_detailed_logs.txt for details."
            )
        raise RuntimeError("Could not parse test summary from run_brew_tests_release.sh output.")
    failed_raw = int(match.group(2))
    total = int(match.group(3))
    failed_names = parse_failed_test_names(output)
    discovered = parse_discovered_test_names(cwd)
    discovered_set = set(discovered)
    def is_ignored(name: str) -> bool:
        if name in ignored_failed_tests:
            return True
        return any(name.startswith(prefix) for prefix in ignored_test_prefixes)

    ignored = [name for name in failed_names if is_ignored(name)]
    non_ignored = [name for name in failed_names if not is_ignored(name)]
    ignored_present = sorted(name for name in discovered_set if is_ignored(name))
    non_ignored_discovered = sorted(name for name in discovered_set if not is_ignored(name))
    non_ignored_failed_set = set(non_ignored)
    non_ignored_passed = [name for name in non_ignored_discovered if name not in non_ignored_failed_set]
    generated_discovered = sorted(name for name in discovered_set if name.startswith("PeanutButterUltimaGenerated_"))
    generated_failed = [name for name in non_ignored if name.startswith("PeanutButterUltimaGenerated_")]
    generated_failed_set = set(generated_failed)
    generated_passed = [name for name in generated_discovered if name not in generated_failed_set]
    failed_effective = len(non_ignored)
    total_effective = len(non_ignored_discovered)
    return BrewRunResult(
        failed_tests_raw=failed_raw,
        failed_tests_effective=failed_effective,
        total_tests_effective=total_effective,
        ignored_failed_tests=ignored,
        ignored_present_tests=ignored_present,
        non_ignored_failed_tests=non_ignored,
        non_ignored_passed_tests=non_ignored_passed,
        generated_total=len(generated_discovered),
        generated_failed=len(generated_failed),
        generated_passed=generated_passed,
        generated_failed_tests=generated_failed,
        total_tests=total,
        return_code=return_code,
        raw_output=output,
    )


def report_phase(
    report_lines: list[str],
    *,
    n: int,
    last_n: int,
    phase_label: str,
    phase_expectation: str,
    expect_exec: int,
    expect_gen: int,
    actual_exec: int | None,
    actual_gen: int | None,
    file_checks_passed: bool,
    ignored_failures: list[str],
    ignored_present_tests: list[str],
    non_ignored_failed_tests: list[str],
    non_ignored_passed_tests: list[str],
    effective_failures: int,
    effective_total: int,
) -> bool:
    report_lines.append(f"[N = {n}, LAST_N = {last_n}]")
    sizes_known = actual_exec is not None and actual_gen is not None
    status = sizes_known and actual_exec == expect_exec and actual_gen == expect_gen
    status_text = "Match!" if status else "Mismatch, Exiting!"
    file_status = "Passed" if file_checks_passed else "Mismatch, Exiting!"
    report_lines.append(
        f"{phase_label} {phase_expectation} "
        f"(src/FormatConstants.hpp, tests/generated/test_cases_all.json, run_brew_tests_detailed_logs.txt {file_status})"
    )
    exec_text = "?" if actual_exec is None else str(actual_exec)
    gen_text = "?" if actual_gen is None else str(actual_gen)
    report_lines.append(
        f"    i.) L1 Sizes ({exec_text}, {gen_text}) Expected ({expect_exec}, {expect_gen}) [{status_text}]"
    )
    report_lines.append(
        f"    ii.) Effective failed tests: {effective_failures}/{effective_total}; "
        f"Ignored failures: {ignored_failures or ['none']}; Ignored tests present: {ignored_present_tests or ['none']}"
    )
    report_lines.append(f"    iii.) Effective failed tests list: {non_ignored_failed_tests or ['none']}")
    report_lines.append(f"    iv.) Effective passed tests list: {non_ignored_passed_tests or ['none']}")
    return status


def main() -> None:
    parser = argparse.ArgumentParser(description="Automate L1 payload sweep with stale-vs-regenerated gate checks.")
    parser.add_argument("--start", type=int, default=2, help="Start N value (inclusive).")
    parser.add_argument("--end", type=int, default=32, help="End N value (inclusive).")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent
    format_constants = repo_root / "src/FormatConstants.hpp"
    generator_script = repo_root / "test_case_gen_all.py"
    generated_cases_json = repo_root / "tests/generated/test_cases_all.json"
    report_path = repo_root / "automate_l1_payload_sweep_output.txt"
    ignored_failed_tests: set[str] = set()
    ignored_test_prefixes = ("PeanutButterUltimaCodex_",)
    allowed_mismatch_generated_passers = {
        "PeanutButterUltimaGenerated_Test_Fences_EndOfFile_TrailingArchives"
    }
    report_lines: list[str] = []

    if args.start < 2 or args.end < args.start:
        raise SystemExit("Invalid range: require 2 <= start <= end.")

    last_n = 1
    try:
        print(f"[INIT] LAST_N={last_n}")
        set_test_payload_size_n(format_constants, last_n)
        print(f"[INIT] SB_PAYLOAD_SIZE = SB_RECOVERY_HEADER_LENGTH + {last_n}")
        run_command([sys.executable, str(generator_script)], repo_root)
        recovery_header_len = parse_recovery_header_length(format_constants)

        for n in range(args.start, args.end + 1):
            print(f"\n[STEP] N={n} (LAST_N={last_n})")
            set_test_payload_size_n(format_constants, n)
            print(f"[STEP] SB_PAYLOAD_SIZE = SB_RECOVERY_HEADER_LENGTH + {n}")

            stale_run = run_brew_release_and_parse(
                repo_root,
                ignored_failed_tests=ignored_failed_tests,
                ignored_test_prefixes=ignored_test_prefixes,
            )
            stale_exec = parse_test_execution_l1_payload_length(format_constants)
            stale_gen = parse_generation_l1_payload(generated_cases_json)
            stale_files_ok = stale_exec is not None and stale_gen is not None and generated_cases_json.exists()
            stale_ok = report_phase(
                report_lines,
                n=n,
                last_n=last_n,
                phase_label="a.)",
                phase_expectation="Mismatch Expected.",
                expect_exec=recovery_header_len + n,
                expect_gen=recovery_header_len + last_n,
                actual_exec=stale_exec,
                actual_gen=stale_gen,
                file_checks_passed=stale_files_ok,
                ignored_failures=stale_run.ignored_failed_tests,
                ignored_present_tests=stale_run.ignored_present_tests,
                non_ignored_failed_tests=stale_run.non_ignored_failed_tests,
                non_ignored_passed_tests=stale_run.non_ignored_passed_tests,
                effective_failures=stale_run.failed_tests_effective,
                effective_total=stale_run.total_tests_effective,
            )
            if not stale_ok:
                report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
                raise RuntimeError(
                    f"Geometry mismatch at N={n}: got (exec={stale_exec}, gen={stale_gen}), "
                    f"expected (exec={recovery_header_len + n}, gen={recovery_header_len + last_n})."
                )
            report_lines.append(
                f"    v.) Generated tests failed: {stale_run.generated_failed}/{stale_run.generated_total}; "
                f"Generated passed list: {stale_run.generated_passed or ['none']}"
            )
            unexpected_generated_passers = [
                name for name in stale_run.generated_passed if name not in allowed_mismatch_generated_passers
            ]
            report_lines.append(
                "    vi.) PASSED TESTS WHICH WERE EXPECTED TO FAIL: "
                f"{stale_run.generated_passed or ['none']}"
            )
            report_lines.append(
                f"    vii.) Allowed mismatch passers: {sorted(allowed_mismatch_generated_passers)}"
            )
            report_lines.append(
                "    viii.) Unexpected mismatch passers: "
                f"{unexpected_generated_passers or ['none']}"
            )
            if unexpected_generated_passers:
                report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
                raise RuntimeError(
                    f"Gate failed at N={n}: mismatch phase has unexpected generated passers: "
                    f"{unexpected_generated_passers}."
                )
            minimum_expected_failures = max(0, stale_run.generated_total - len(allowed_mismatch_generated_passers))
            if stale_run.generated_failed < minimum_expected_failures:
                report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
                raise RuntimeError(
                    f"Gate failed at N={n}: mismatch phase expected at least {minimum_expected_failures} generated "
                    f"failures (all but allowed passers), got {stale_run.generated_failed}/{stale_run.generated_total}."
                )
            print(
                f"[CHECK] stale generated failures={stale_run.generated_failed}/{stale_run.generated_total} "
                f"(expected all fail except allowed passers)"
            )

            run_command([sys.executable, str(generator_script)], repo_root)
            regenerated_run = run_brew_release_and_parse(
                repo_root,
                ignored_failed_tests=ignored_failed_tests,
                ignored_test_prefixes=ignored_test_prefixes,
            )
            regen_exec = parse_test_execution_l1_payload_length(format_constants)
            regen_gen = parse_generation_l1_payload(generated_cases_json)
            regen_files_ok = regen_exec is not None and regen_gen is not None and generated_cases_json.exists()
            regen_ok = report_phase(
                report_lines,
                n=n,
                last_n=last_n,
                phase_label="b.)",
                phase_expectation="Match Expected.",
                expect_exec=recovery_header_len + n,
                expect_gen=recovery_header_len + n,
                actual_exec=regen_exec,
                actual_gen=regen_gen,
                file_checks_passed=regen_files_ok,
                ignored_failures=regenerated_run.ignored_failed_tests,
                ignored_present_tests=regenerated_run.ignored_present_tests,
                non_ignored_failed_tests=regenerated_run.non_ignored_failed_tests,
                non_ignored_passed_tests=regenerated_run.non_ignored_passed_tests,
                effective_failures=regenerated_run.failed_tests_effective,
                effective_total=regenerated_run.total_tests_effective,
            )
            report_lines.append("")
            report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
            if not regen_ok:
                raise RuntimeError(
                    f"Geometry mismatch after regeneration at N={n}: got (exec={regen_exec}, gen={regen_gen}), "
                    f"expected (exec={recovery_header_len + n}, gen={recovery_header_len + n})."
                )
            if regenerated_run.failed_tests_effective != 0:
                raise RuntimeError(
                    f"Gate failed at N={n}: regenerated suite produced {regenerated_run.failed_tests_effective} "
                    f"effective failed tests (raw={regenerated_run.failed_tests_raw}); expected 0. "
                    f"Failers: {regenerated_run.non_ignored_failed_tests or 'none'}"
                )
            print(
                f"[CHECK] regenerated suite failures effective={regenerated_run.failed_tests_effective}/"
                f"{regenerated_run.total_tests} raw={regenerated_run.failed_tests_raw} (expected effective 0)"
            )

            last_n = n
            print(f"[OK] LAST_N={last_n}")

        print(f"\n[DONE] Sweep completed through N={args.end}. Final LAST_N={last_n}.")
        print(f"[REPORT] {report_path}")
    except Exception as exc:
        report_lines.append("")
        report_lines.append(f"[ERROR] {exc}")
        report_lines.append("See run_brew_tests_detailed_logs.txt for detailed build/test logs.")
        report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
        print(f"[ERROR] {exc}")
        print(f"[REPORT] {report_path}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
