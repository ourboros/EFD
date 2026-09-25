#!/bin/bash
# ==============================================================================
# EFD macOS .app Bundle 自動化建置腳本
# ==============================================================================
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/macos"
APP_BUNDLE="${PROJECT_ROOT}/build/EFD.app"

echo "=================================================================="
echo "  [EFD] 開始建置 macOS 原生應用程式 (EFD.app)..."
echo "=================================================================="

mkdir -p "${BUILD_DIR}"
mkdir -p "${PROJECT_ROOT}/build"

# 1. 建立 EFD.app 標準目錄結構
rm -rf "${APP_BUNDLE}"
mkdir -p "${APP_BUNDLE}/Contents/MacOS"
mkdir -p "${APP_BUNDLE}/Contents/Resources"

# 2. 複製 Info.plist
cp "${PROJECT_ROOT}/macos/Info.plist" "${APP_BUNDLE}/Contents/Info.plist"

# 3. 複製資產至 Resources
if [ -d "${PROJECT_ROOT}/assets" ]; then
    cp -r "${PROJECT_ROOT}/assets" "${APP_BUNDLE}/Contents/Resources/"
fi
if [ -d "${PROJECT_ROOT}/design" ]; then
    cp -r "${PROJECT_ROOT}/design" "${APP_BUNDLE}/Contents/Resources/"
fi
LOGO_SRC=""
for candidate in "${PROJECT_ROOT}/design/1x/資產 10.png" "${PROJECT_ROOT}/design/1x/logo10.png" "${PROJECT_ROOT}/assets/logo.png"; do
    if [ -f "${candidate}" ]; then
        LOGO_SRC="${candidate}"
        break
    fi
done

if [ -n "${LOGO_SRC}" ]; then
    cp "${LOGO_SRC}" "${APP_BUNDLE}/Contents/Resources/資產 10.png" 2>/dev/null || true
    cp "${LOGO_SRC}" "${APP_BUNDLE}/Contents/Resources/logo10.png" 2>/dev/null || true
    cp "${LOGO_SRC}" "${APP_BUNDLE}/Contents/Resources/logo.png" 2>/dev/null || true
fi

# 4. 生成 macOS 原生 AppIcon.icns
if [ -n "${LOGO_SRC}" ] && command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1; then
    echo " -> 正在生成 Retina 高解析度應用程式圖示 (AppIcon.icns)..."
    ICONSET_DIR="${BUILD_DIR}/AppIcon.iconset"
    rm -rf "${ICONSET_DIR}"
    mkdir -p "${ICONSET_DIR}"
    sips -z 16 16     "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_16x16.png" >/dev/null 2>&1 || true
    sips -z 32 32     "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_16x16@2x.png" >/dev/null 2>&1 || true
    sips -z 32 32     "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_32x32.png" >/dev/null 2>&1 || true
    sips -z 64 64     "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_32x32@2x.png" >/dev/null 2>&1 || true
    sips -z 128 128   "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_128x128.png" >/dev/null 2>&1 || true
    sips -z 256 256   "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_128x128@2x.png" >/dev/null 2>&1 || true
    sips -z 256 256   "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_256x256.png" >/dev/null 2>&1 || true
    sips -z 512 512   "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_256x256@2x.png" >/dev/null 2>&1 || true
    sips -z 512 512   "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_512x512.png" >/dev/null 2>&1 || true
    sips -z 1024 1024 "${LOGO_SRC}" --out "${ICONSET_DIR}/icon_512x512@2x.png" >/dev/null 2>&1 || true
    iconutil -c icns "${ICONSET_DIR}" -o "${APP_BUNDLE}/Contents/Resources/AppIcon.icns" >/dev/null 2>&1 || true
    rm -rf "${ICONSET_DIR}"
fi

# 5. 編譯二進制檔案
if command -v cmake >/dev/null 2>&1; then
    echo " -> 偵測到 CMake，使用 CMake 進行建置..."
    cd "${BUILD_DIR}"
    cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DEFD_BUILD_TESTS=OFF \
        -DEFD_BUILD_CLI=OFF
    cmake --build . --config Release -j$(sysctl -n hw.ncpu)
    
    if [ -f "${BUILD_DIR}/efd_gui.app/Contents/MacOS/efd_gui" ]; then
        cp "${BUILD_DIR}/efd_gui.app/Contents/MacOS/efd_gui" "${APP_BUNDLE}/Contents/MacOS/efd_gui"
    elif [ -f "${BUILD_DIR}/efd_gui" ]; then
        cp "${BUILD_DIR}/efd_gui" "${APP_BUNDLE}/Contents/MacOS/efd_gui"
    elif [ -f "${BUILD_DIR}/Release/efd_gui" ]; then
        cp "${BUILD_DIR}/Release/efd_gui" "${APP_BUNDLE}/Contents/MacOS/efd_gui"
    fi
else
    echo " -> 未安裝 CMake，切換為 Apple Clang++ 直接編譯..."
    clang++ -std=c++20 -O3 \
        -I"${PROJECT_ROOT}/include" \
        -I"${PROJECT_ROOT}/src" \
        "${PROJECT_ROOT}/src/analysis/FeatureExtractor.cpp" \
        "${PROJECT_ROOT}/src/analysis/EmdCalculator.cpp" \
        "${PROJECT_ROOT}/src/analysis/MseCalculator.cpp" \
        "${PROJECT_ROOT}/src/analysis/AdaptiveBaseline.cpp" \
        "${PROJECT_ROOT}/src/state/FatigueStateMachine.cpp" \
        "${PROJECT_ROOT}/src/vision/FaceLandmarker.cpp" \
        "${PROJECT_ROOT}/src/vision/CameraService.cpp" \
        "${PROJECT_ROOT}/src/vision/VisionPipeline.cpp" \
        "${PROJECT_ROOT}/src/vision/drivers/SyntheticCameraDriver.cpp" \
        "${PROJECT_ROOT}/src/platform/PlatformLifecycleAdapter.cpp" \
        "${PROJECT_ROOT}/src/storage/DatabaseService.cpp" \
        "${PROJECT_ROOT}/src/study/StudyWorkflowTracker.cpp" \
        "${PROJECT_ROOT}/src/network/NetworkSyncWorker.cpp" \
        "${PROJECT_ROOT}/src/engine/AsyncPipelineEngine.cpp" \
        "${PROJECT_ROOT}/src/ui/main_gui.cpp" \
        "${PROJECT_ROOT}/src/ui/macos/MacWelcomeWindow.mm" \
        "${PROJECT_ROOT}/src/platform/macos/MacStatusItemManager.mm" \
        -framework Cocoa \
        -framework AppKit \
        -framework Foundation \
        -framework AVFoundation \
        -framework QuartzCore \
        -framework UserNotifications \
        -o "${APP_BUNDLE}/Contents/MacOS/efd_gui"
fi

chmod +x "${APP_BUNDLE}/Contents/MacOS/efd_gui"

# 6. 執行 Ad-Hoc 程式碼簽名
if command -v codesign >/dev/null 2>&1; then
    echo " -> 正在套用 Ad-Hoc 程式碼簽名..."
    codesign --force --deep --sign - "${APP_BUNDLE}"
fi

echo "=================================================================="
echo "  [SUCCESS] macOS 原生應用程式建置完成！"
echo "  Bundle 路徑: ${APP_BUNDLE}"
echo "=================================================================="
