#!/bin/bash
# ==============================================================================
# EFD macOS .app Bundle 自動化建置腳本
# ==============================================================================
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/macos"
APP_BUNDLE="${BUILD_DIR}/EFD.app"

echo "=== [1/4] 開始建置 EFD macOS 原生二進制檔案 ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${PROJECT_ROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DEFD_BUILD_TESTS=OFF \
    -DEFD_BUILD_CLI=ON

cmake --build . --config Release -j$(sysctl -n hw.ncpu)

echo "=== [2/4] 建立 macOS 應用程式目錄結構 (EFD.app) ==="
rm -rf "${APP_BUNDLE}"
mkdir -p "${APP_BUNDLE}/Contents/MacOS"
mkdir -p "${APP_BUNDLE}/Contents/Resources"

cp "${BUILD_DIR}/efd_gui" "${APP_BUNDLE}/Contents/MacOS/efd_gui" 2>/dev/null || cp "${BUILD_DIR}/Release/efd_gui" "${APP_BUNDLE}/Contents/MacOS/efd_gui" 2>/dev/null || cp "${BUILD_DIR}/efd_cli" "${APP_BUNDLE}/Contents/MacOS/efd_gui"
chmod +x "${APP_BUNDLE}/Contents/MacOS/efd_gui"

cp "${PROJECT_ROOT}/macos/Info.plist" "${APP_BUNDLE}/Contents/Info.plist"
if [ -d "${PROJECT_ROOT}/assets" ]; then
    cp -r "${PROJECT_ROOT}/assets" "${APP_BUNDLE}/Contents/Resources/"
fi

echo "=== [3/4] 執行 Ad-Hoc 程式碼簽名 ==="
codesign --force --deep --sign - "${APP_BUNDLE}"

echo "=== [4/4] macOS EFD.app 建置完成！==="
echo "路徑: ${APP_BUNDLE}"
