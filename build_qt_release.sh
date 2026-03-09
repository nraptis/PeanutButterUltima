#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_release"

rm -rf "${BUILD_DIR}"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --config Release --target PeanutButterUltimaCore PeanutButterUltimaShell

if cmake --build "${BUILD_DIR}" --config Release --target PeanutButterUltima >/dev/null 2>&1; then
  cmake --build "${BUILD_DIR}" --config Release --target PeanutButterUltima
  QT_TARGET_BUILT=true
else
  QT_TARGET_BUILT=false
fi

echo "Release outputs:"
echo "  Library:   ${BUILD_DIR}/libPeanutButterUltimaCore.a"
echo "  Shell exe: ${BUILD_DIR}/PeanutButterUltimaShell"

if [ "${QT_TARGET_BUILT}" = true ]; then
  if [ -d "${BUILD_DIR}/PeanutButterUltima.app" ]; then
    echo "  QT exe:    ${BUILD_DIR}/PeanutButterUltima.app/Contents/MacOS/PeanutButterUltima"
  else
    echo "  QT exe:    ${BUILD_DIR}/PeanutButterUltima"
  fi
else
  echo "Qt target not built. PeanutButterUltima unavailable in this environment."
fi
