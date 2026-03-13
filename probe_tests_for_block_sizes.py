#!/usr/bin/env python3

from __future__ import annotations

import itertools
import re
import subprocess
from pathlib import Path
from typing import Iterable, List, Tuple


BLOCK_SIZE_MIN = 48
BLOCK_SIZE_MAX = 48
TEST_SEEDS = [777]
TEST_LOOPS = [1]


ROOT = Path(__file__).resolve().parent
PB_HEADER_PATH = ROOT / "src" / "PeanutButter.hpp"
REPORT_PATH = ROOT / "probe_tests_report.txt"
BUILD_DIR = ROOT / "build-test-release"
TEST_BIN = BUILD_DIR / "PeanutButterCodexTests"

TAG_L3 = "/*T-L3*/"
TAG_SEED = "/*T-SEED*/"
TAG_LOOPS = "/*T-LOOPS*/"


def _replace_tagged_define_line(text: str, tag: str, value: int) -> str:
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if tag not in line:
            continue
        match = re.match(rf"^(?P<prefix>\s*#\s*define\s+\w+\s+)(?P<body>.*?)(?P<suffix>\s*{re.escape(tag)}.*)$",
                         line.rstrip("\n"))
        if not match:
            raise RuntimeError(f"Could not parse tagged define line for {tag!r}: {line.rstrip()!r}")

        body = match.group("body")
        number_match = re.search(r"\d+", body)
        if number_match is None:
            raise RuntimeError(f"Could not find numeric token for {tag!r}: {line.rstrip()!r}")
        start, end = number_match.span()
        updated_body = body[:start] + str(value) + body[end:]

        lines[i] = f"{match.group('prefix')}{updated_body}{match.group('suffix')}\n"
        return "".join(lines)
    raise RuntimeError(f"Tag {tag!r} was not found in {PB_HEADER_PATH}")


def write_test_config(block_size_l3: int, test_seed: int, test_loops: int) -> None:
    text = PB_HEADER_PATH.read_text(encoding="utf-8")
    text = _replace_tagged_define_line(text, TAG_L3, block_size_l3)
    text = _replace_tagged_define_line(text, TAG_SEED, test_seed)
    text = _replace_tagged_define_line(text, TAG_LOOPS, test_loops)
    PB_HEADER_PATH.write_text(text, encoding="utf-8")


def discover_random_test_files() -> List[Path]:
    random_dir = ROOT / "tests" / "random"
    return sorted(path for path in random_dir.glob("Test*Random.cpp"))


def run_cmd(args: Iterable[str]) -> Tuple[int, str]:
    proc = subprocess.run(
        list(args),
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, output.strip()


def extract_failure_summary(output: str) -> str:
    if not output:
        return "no output"
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("[FAIL]") or line.startswith("CMake Error") or line.startswith("FAILED:"):
            return line
    return output.splitlines()[-1].strip()


def run_probe() -> None:
    original_header = PB_HEADER_PATH.read_text(encoding="utf-8")
    test_files = discover_random_test_files()
    report_lines: List[str] = []
    pass_count = 0
    fail_count = 0

    try:
        for block_size_l3, test_seed, test_loops in itertools.product(
            range(BLOCK_SIZE_MIN, BLOCK_SIZE_MAX + 1),
            TEST_SEEDS,
            TEST_LOOPS,
        ):
            if block_size_l3 <= 0:
                for test_file in test_files:
                    fail_count += 1
                    report_lines.append(
                        f"[Fail] {test_file.name} L3={block_size_l3} SEED={test_seed} LOOPS={test_loops} :: invalid BLOCK_SIZE_L3 payload bytes (must be > 0)"
                    )
                continue

            write_test_config(block_size_l3, test_seed, test_loops)

            build_rc, build_output = run_cmd(
                ["cmake", "--build", str(BUILD_DIR), "-j4", "--target", "PeanutButterCodexTests"]
            )
            if build_rc != 0:
                summary = extract_failure_summary(build_output)
                for test_file in test_files:
                    fail_count += 1
                    report_lines.append(
                        f"[Fail] {test_file.name} L3={block_size_l3} SEED={test_seed} LOOPS={test_loops} :: build failed: {summary}"
                    )
                continue

            for test_file in test_files:
                case_name = test_file.stem
                rc, output = run_cmd([str(TEST_BIN), case_name])
                if rc == 0:
                    pass_count += 1
                    report_lines.append(
                        f"[Pass] {test_file.name} L3={block_size_l3} SEED={test_seed} LOOPS={test_loops}"
                    )
                else:
                    fail_count += 1
                    summary = extract_failure_summary(output)
                    report_lines.append(
                        f"[Fail] {test_file.name} L3={block_size_l3} SEED={test_seed} LOOPS={test_loops} :: {summary}"
                    )
    finally:
        PB_HEADER_PATH.write_text(original_header, encoding="utf-8")

    report_body = [f"{pass_count} Passes {fail_count} Failures", ""] + report_lines
    REPORT_PATH.write_text("\n".join(report_body) + "\n", encoding="utf-8")
    print(f"Wrote {REPORT_PATH} with {pass_count} passes and {fail_count} failures.")


if __name__ == "__main__":
    run_probe()
