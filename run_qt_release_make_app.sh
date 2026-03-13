#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-release"
ICON_SRC="${SCRIPT_DIR}/icon.jpg"
ICON_PNG_SRC="${SCRIPT_DIR}/icon.png"
ICONSET_SRC="${SCRIPT_DIR}/icon.iconset"
TMP_ICONSET_DIR="${BUILD_DIR}/icon.iconset"
TMP_ICNS_PATH="${BUILD_DIR}/icon.icns"

generate_icns_with_pillow() {
  local src_png="$1"
  local out_icns="$2"
  python3 - "$src_png" "$out_icns" <<'PY'
import sys
from PIL import Image

src = sys.argv[1]
out = sys.argv[2]
img = Image.open(src).convert("RGBA")
sizes = [(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)]
img.save(out, format="ICNS", sizes=sizes)
PY
}

rm -rf "${BUILD_DIR}"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --config Release --target PeanutButterUltima
QT_TARGET_BUILT=true

echo "Release outputs:"

if [ "${QT_TARGET_BUILT}" = true ]; then
  if [ -d "${BUILD_DIR}/PeanutButterUltima.app" ]; then
    APP_RESOURCES_DIR="${BUILD_DIR}/PeanutButterUltima.app/Contents/Resources"
    APP_INFO_PLIST="${BUILD_DIR}/PeanutButterUltima.app/Contents/Info.plist"
    mkdir -p "${APP_RESOURCES_DIR}"
    if [ -f "${ICON_SRC}" ]; then
      cp -f "${ICON_SRC}" "${APP_RESOURCES_DIR}/icon.jpg"
    fi
    if [ -f "${ICON_PNG_SRC}" ]; then
      cp -f "${ICON_PNG_SRC}" "${APP_RESOURCES_DIR}/icon.png"
    fi

    ICON_PACKAGED=false
    if [ -d "${ICONSET_SRC}" ]; then
      if iconutil -c icns "${ICONSET_SRC}" -o "${TMP_ICNS_PATH}" >/dev/null 2>&1; then
        ICON_PACKAGED=true
      elif [ -f "${ICON_PNG_SRC}" ]; then
        generate_icns_with_pillow "${ICON_PNG_SRC}" "${TMP_ICNS_PATH}"
        ICON_PACKAGED=true
      fi
    elif [ -f "${ICON_PNG_SRC}" ]; then
      rm -rf "${TMP_ICONSET_DIR}"
      mkdir -p "${TMP_ICONSET_DIR}"
      sips -s format png -z 16 16   "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_16x16.png" >/dev/null
      sips -s format png -z 32 32   "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_16x16@2x.png" >/dev/null
      sips -s format png -z 32 32   "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_32x32.png" >/dev/null
      sips -s format png -z 64 64   "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_32x32@2x.png" >/dev/null
      sips -s format png -z 128 128 "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_128x128.png" >/dev/null
      sips -s format png -z 256 256 "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_128x128@2x.png" >/dev/null
      sips -s format png -z 256 256 "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_256x256.png" >/dev/null
      sips -s format png -z 512 512 "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_256x256@2x.png" >/dev/null
      sips -s format png -z 512 512 "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_512x512.png" >/dev/null
      sips -s format png -z 1024 1024 "${ICON_PNG_SRC}" --out "${TMP_ICONSET_DIR}/icon_512x512@2x.png" >/dev/null
      if iconutil -c icns "${TMP_ICONSET_DIR}" -o "${TMP_ICNS_PATH}" >/dev/null 2>&1; then
        ICON_PACKAGED=true
      else
        generate_icns_with_pillow "${ICON_PNG_SRC}" "${TMP_ICNS_PATH}"
        ICON_PACKAGED=true
      fi
    fi

    if [ "${ICON_PACKAGED}" = true ]; then
      cp -f "${TMP_ICNS_PATH}" "${APP_RESOURCES_DIR}/icon.icns"

      if [ -f "${APP_INFO_PLIST}" ]; then
        /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile icon.icns" "${APP_INFO_PLIST}" >/dev/null 2>&1 || \
          /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string icon.icns" "${APP_INFO_PLIST}" >/dev/null 2>&1
      fi
    fi
    echo "  QT exe:    ${BUILD_DIR}/PeanutButterUltima.app/Contents/MacOS/PeanutButterUltima"
    if [ -f "${APP_RESOURCES_DIR}/icon.jpg" ]; then
      echo "  QT icon:   ${APP_RESOURCES_DIR}/icon.jpg"
    else
      echo "  QT icon:   icon.jpg not found at repo root; skipped packaging icon."
    fi
    if [ -f "${APP_RESOURCES_DIR}/icon.icns" ]; then
      echo "  QT icon2:  ${APP_RESOURCES_DIR}/icon.icns"
    else
      echo "  QT icon2:  icon.icns not generated (provide icon.iconset or icon.png)."
    fi
  else
    if [ -f "${ICON_SRC}" ]; then
      cp -f "${ICON_SRC}" "${BUILD_DIR}/icon.jpg"
    fi
    echo "  QT exe:    ${BUILD_DIR}/PeanutButterUltima"
    if [ -f "${BUILD_DIR}/icon.jpg" ]; then
      echo "  QT icon:   ${BUILD_DIR}/icon.jpg"
    else
      echo "  QT icon:   icon.jpg not found at repo root; skipped packaging icon."
    fi
  fi
else
  echo "Qt target not built. PeanutButterUltima unavailable in this environment."
fi
