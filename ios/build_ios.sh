#!/bin/bash
# ==============================================================================
# EFD iOS Xcode 專案與 .ipa 自動化建置腳本
# ==============================================================================
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/ios"

echo "=== [1/3] 生成 iOS Xcode 專案 (CMake iOS Toolchain) ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${PROJECT_ROOT}" \
    -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="" \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="Apple Development" \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED="NO" \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED="NO"

echo "=== [2/3] 編譯 iOS 專案 (Simulator / Device) ==="
xcodebuild -project EFD.xcodeproj \
           -scheme EFD_iOS \
           -configuration Release \
           -sdk iphoneos \
           CODE_SIGN_IDENTITY="" \
           CODE_SIGNING_REQUIRED=NO \
           CODE_SIGNING_ALLOWED=NO \
           build || echo "請在 macOS 環境中使用 Xcode 開啟 ${BUILD_DIR}/EFD.xcodeproj 進行側載簽名安裝。"

echo "=== [3/3] iOS 專案結構建置完成！==="
echo "Xcode 專案路徑: ${BUILD_DIR}/EFD.xcodeproj"

