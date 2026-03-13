#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN_PATH="example_old/generate_example_bin"

echo "Building current-format example generator"
c++ -std=c++17 -Isrc \
  example_old/main_generate_example.cpp \
  src/AppShell_ArchiveFormat.cpp \
  src/AppShell_Bundle.cpp \
  src/AppShell_Common.cpp \
  src/AppShell_Types.cpp \
  src/Encryption/Crypt.cpp \
  src/IO/FileSystem.cpp \
  src/IO/LocalFileSystem.cpp \
  -o "$BIN_PATH"

echo "Running generator"
"$BIN_PATH"

echo "RESULT: PASS"
