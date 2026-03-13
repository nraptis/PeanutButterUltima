#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

echo "Building test starter kit example..."
c++ -std=c++17 \
  -I./src \
  -I./test_starter_kit/include \
  test_starter_kit/examples/ExampleApiAccess.cpp \
  test_starter_kit/src/MockHardDrive.cpp \
  test_starter_kit/src/MockFileSystem.cpp \
  test_starter_kit/src/EmptyTestCrypt.cpp \
  test_starter_kit/src/StarterKitHelpers.cpp \
  test_starter_kit/src/TestScenarioHelpers.cpp \
  src/AppShell_ArchiveFormat.cpp \
  src/AppShell_Bundle.cpp \
  src/AppShell_Common.cpp \
  src/AppShell_Extended_Types.cpp \
  src/AppShell_Extended_Bundle.cpp \
  src/AppShell_Sanity.cpp \
  src/AppShell_Types.cpp \
  src/AppShell_Unbundle.cpp \
  src/IO/FileSystem.cpp \
  -o test_starter_kit/starter_kit_example

echo "Running example..."
./test_starter_kit/starter_kit_example
