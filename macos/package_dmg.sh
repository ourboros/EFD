#!/bin/bash
# ==============================================================================
# EFD macOS .dmg 安裝映像檔自動化打包腳本
# ==============================================================================
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_BUNDLE="${PROJECT_ROOT}/build/EFD.app"
DMG_STAGE="${PROJECT_ROOT}/build/dmg_stage"
DMG_OUTPUT="${PROJECT_ROOT}/build/EFD-v1.0.0-macOS.dmg"

echo "=================================================================="
echo "  [EFD] 開始打包 macOS .dmg 安裝映像檔..."
echo "=================================================================="

# 1. 確保 EFD.app 存在，若不存在則先執行建置
if [ ! -d "${APP_BUNDLE}" ]; then
    echo " -> 找不到 ${APP_BUNDLE}，自動執行建置程序..."
    bash "${PROJECT_ROOT}/macos/build_macos.sh"
fi

# 2. 準備 DMG 打包暫存目錄
echo " -> 準備 DMG 打包暫存目錄與 Applications 捷徑..."
rm -rf "${DMG_STAGE}"
mkdir -p "${DMG_STAGE}"
cp -r "${APP_BUNDLE}" "${DMG_STAGE}/"
ln -s /Applications "${DMG_STAGE}/Applications"

# 3. 生成 DMG
echo " -> 正在生成 UDZO 壓縮格式之 .dmg 安裝映像檔..."
rm -f "${DMG_OUTPUT}"
hdiutil create -volname "EFD-Installer" -srcfolder "${DMG_STAGE}" -ov -format UDZO "${DMG_OUTPUT}"

# 4. 清理暫存
rm -rf "${DMG_STAGE}"

echo "=================================================================="
echo "  [SUCCESS] macOS DMG 安裝映像檔打包完成！"
echo "  輸出檔案: ${DMG_OUTPUT}"
echo "=================================================================="
