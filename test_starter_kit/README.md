# Test Starter Kit

This folder is a black-box test harness starter for the current PeanutButter engine format.
It is designed for adversarial test generation where the model/test-author does **not** read engine code.

## What is included

- `include/MockHardDrive.hpp` + `src/MockHardDrive.cpp`
  - In-memory hard drive model.
  - Directory + file tree management.
  - Fault injection (`AddReadFault`, `AddWriteFault`).
  - Delete and truncate helpers for archive-file mutation scenarios.
- `include/MockFileSystem.hpp` + `src/MockFileSystem.cpp`
  - `FileSystem` implementation backed by `MockHardDrive`.
  - Supports recursive listing, streams, append, path utilities.
- `include/EmptyTestCrypt.hpp` + `src/EmptyTestCrypt.cpp`
  - No-op crypt implementation for deterministic test flows.
  - Optional forced failures for crypt-error tests.
- `include/StarterKitHelpers.hpp` + `src/StarterKitHelpers.cpp`
  - Build `SourceEntry` vectors from a source tree.
  - List sorted archive files.
  - Snapshot/compare trees for round-trip assertions.
  - Inspect archive headers and recovery headers with checksum verification.
- `include/DeterministicRng.hpp`
  - Deterministic RNG utilities for seed-based repeatability.
- `include/TestScenarioHelpers.hpp` + `src/TestScenarioHelpers.cpp`
  - Random test file generation utilities matching the test suite spec model.
- `examples/ExampleApiAccess.cpp`
  - End-to-end usage examples for:
    - `Bundle`
    - `Unbundle`
    - `Recover`
    - `BundleWithMutations`
    - `RunSanity` (validate)
- `ARCHIVE_LAYOUT_FORMAT.md`
  - Byte-accurate archive layout contract.
- `run_starter_kit_example.sh`
  - Builds and runs the example with current engine sources.

## Why this kit is useful for adversarial tests

From an objective testing perspective, the adversarial model needs more than only API calls:

1. Deterministic randomness and reproducibility.
2. A controllable filesystem with fault injection and direct archive mutation controls.
3. Independent format inspection (header + checksum checks) to validate engine output.
4. Tree-level comparison helpers to validate round-trip and recovery outputs.
5. Examples showing both healthy and broken workflows.

This starter kit provides all five.

## Quick start

From repo root:

```bash
./test_starter_kit/run_starter_kit_example.sh
```

The example prints operation results and archive-inspection summaries.

## Typical usage pattern for tests

1. Use `MockFileSystem` + `EmptyTestCrypt`.
2. Materialize source files into `/input` (or similar).
3. Build `SourceEntry` list with `BuildSourceEntriesFromDirectory`.
4. Run `Bundle` and inspect archives with `InspectArchiveSet`.
5. Run `Unbundle` and compare trees with `SnapshotTree` + `CompareSnapshots`.
6. For adversarial tests:
   - mutate archive files directly through `MockHardDrive`, or
   - use `BundleWithMutations` (`DataMutation`, `CreateFileMutation`, `DeleteFileMutation`).
7. Run `Recover` and compare recovered tree with expected oracle output.

## Notes

- `BundleWithMutations` applies **data mutations before checksum generation**.
- The starter kit keeps path handling POSIX-style (`/`) and deterministic ordering.
- Log-text assertions are intentionally not part of the kit contract.
