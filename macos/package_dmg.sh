#!/bin/bash
# ==============================================================================
# EFD macOS .dmg 安裝檔打包腳本
# ==============================================================================
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/macos"
APP_BUNDLE="${BUILD_DIR}/EFD.app"
DMG_OUTPUT="${PROJECT_ROOT}/build/EFD-v1.0.0-macOS.dmg"
DMG_STAGE="${BUILD_DIR}/dmg_stage"

if [ ! -d "${APP_BUNDLE}" ]; then
    echo "錯誤: 找不到 ${APP_BUNDLE}，請先執行 ./macos/build_macos.sh"
    exit 1
fi

echo "=== [1/3] 準備 DMG 打包暫存目錄 ==="
rm -rf "${DMG_STAGE}"
mkdir -p "${DMG_STAGE}"
cp -r "${APP_BUNDLE}" "${DMG_STAGE}/"
ln -s /Applications "${DMG_STAGE}/Applications"

echo "=== [2/3] 生成 macOS .dmg 磁碟映像檔 ==="
rm -f "${DMG_OUTPUT}"
hdiutil create -volname "EFD-Installer" -srcfolder "${DMG_STAGE}" -ov -format UDZO "${DMG_OUTPUT}"

echo "=== [3/3] macOS DMG 安裝檔打包完成！==="
echo "輸出路徑: ${DMG_OUTPUT}"

