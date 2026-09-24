#!/bin/bash
# ==============================================================================
# EFD (Eye Fatigue Detection) macOS 一鍵安裝、編譯與啟動腳本
# 說明: 在 macOS Finder 中直接雙擊本腳本即可自動完成安裝與啟動
# ==============================================================================
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${DIR}/.." && pwd)"

clear
echo "=================================================================="
echo "    EFD 眼睛特徵提取與即時疲勞監控研究系統 (macOS 原生版)"
echo "=================================================================="
echo "  [歡迎] 本腳本將自動檢測環境、建置 macOS 原生應用並直接啟動測試。"
echo "------------------------------------------------------------------"

# 1. 檢查 Xcode Command Line Tools
if ! command -v clang++ >/dev/null 2>&1; then
    echo " [提示] 尚未安裝 Apple Developer Tools (Clang++)。"
    echo " 正在啟動安裝程序，請在彈出的系統視窗中點擊「安裝」..."
    xcode-select --install
    echo " 請在工具安裝完成後，重新雙擊執行此腳本。"
    read -p " 按 Enter 鍵結束..."
    exit 1
fi

echo " [1/3] 正在執行 macOS 原生應用建置 (EFD.app)..."
bash "${PROJECT_ROOT}/macos/build_macos.sh"

APP_PATH="${PROJECT_ROOT}/build/EFD.app"

# 2. 解除 macOS Gatekeeper 隔離屬性 (避免非 App Store 應用被阻擋)
echo " [2/3] 解除 Gatekeeper 隔離屬性..."
xattr -dr com.apple.quarantine "${APP_PATH}" 2>/dev/null || true

# 3. 詢問是否安裝至 /Applications 目錄
echo "------------------------------------------------------------------"
read -p " 是否將 EFD 安裝至系統「應用程式」目錄 (/Applications)？(y/N): " install_choice
if [[ "$install_choice" =~ ^[Yy]$ ]]; then
    echo " 正在複製 EFD.app 至 /Applications..."
    rm -rf /Applications/EFD.app
    cp -r "${APP_PATH}" /Applications/
    xattr -dr com.apple.quarantine /Applications/EFD.app 2>/dev/null || true
    echo " 安裝完成！已放置於 /Applications/EFD.app"
    echo " [3/3] 正在啟動 EFD..."
    open /Applications/EFD.app
else
    echo " [3/3] 正在直接從建置目錄啟動 EFD..."
    open "${APP_PATH}"
fi

echo "=================================================================="
echo "  [SUCCESS] EFD 原生應用已成功啟動！"
echo "  若主視窗縮小至背景，可於頂部選單列狀態圓點 (Status Bar) 隨時喚回。"
echo "=================================================================="
read -p " 請按 Enter 鍵關閉此視窗..."

