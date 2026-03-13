#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN_PATH="codex_sandbox/roundtrip_smoke_bin"

c++ -std=c++17 -Isrc \
  codex_sandbox/roundtrip_smoke.cpp \
  src/AppShell_ArchiveFormat.cpp \
  src/AppShell_Bundle.cpp \
  src/AppShell_Common.cpp \
  src/AppShell_Sanity.cpp \
  src/AppShell_Types.cpp \
  src/AppShell_Unbundle.cpp \
  src/Encryption/Crypt.cpp \
  src/IO/FileSystem.cpp \
  src/IO/LocalFileSystem.cpp \
  -o "$BIN_PATH"

"$BIN_PATH"
