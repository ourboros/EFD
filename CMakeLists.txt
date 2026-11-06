cmake_minimum_required(VERSION 3.20)

project(EFD 
    VERSION 2.3.1 
    DESCRIPTION "Eye Fatigue Detection Native System"
    LANGUAGES CXX C
)

# 支援平台：Windows (x64，桌面程式) 與 Android (AArch64，經 android/ 的 Gradle + NDK 建置)。
# iOS / macOS 不允許背景使用相機，無法滿足背景守護需求，已移除。

# -----------------------------------------------------------------------------
# C++ Standard & Compiler Options
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Project Options
# -----------------------------------------------------------------------------
option(EFD_BUILD_TESTS "Build unit tests" ON)
option(EFD_BUILD_CLI   "Build CLI simulation/demo application" ON)
option(EFD_ENABLE_QT    "Enable Qt 6 GUI targets (if Qt is found)" AUTO)
option(EFD_WITH_ONNX   "Use MediaPipe face models via ONNX Runtime (heuristic detector when unavailable)" ON)

# ONNX Runtime 由 GitHub Releases 下載解壓到 third_party/onnxruntime (不進版本庫，見 models/README.md)
set(EFD_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH "ONNX Runtime 解壓目錄")
set(EFD_HAS_ONNX OFF)
if(EFD_WITH_ONNX AND WIN32)
    if(EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h" AND EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
        set(EFD_HAS_ONNX ON)
        message(STATUS "ONNX Runtime: ${ONNXRUNTIME_ROOT}")
    else()
        message(WARNING "找不到 ONNX Runtime (${ONNXRUNTIME_ROOT})，改以啟發式臉部偵測建置")
    endif()
endif()

# -----------------------------------------------------------------------------
# Platform & Compiler specific settings
# -----------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

# -----------------------------------------------------------------------------
# Include Directories
# -----------------------------------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# -----------------------------------------------------------------------------
# Core Library Target: efd_core
# -----------------------------------------------------------------------------
set(EFD_CORE_HEADERS
    include/efd/types.hpp
    include/efd/version.hpp
    src/analysis/FeatureExtractor.hpp
    src/analysis/EmdCalculator.hpp
    src/analysis/MseCalculator.hpp
    src/analysis/AdaptiveBaseline.hpp
    src/analysis/RollingOpenBaseline.hpp
    src/analysis/GazeTracker.hpp
    src/analysis/PersonalTrendBaseline.hpp
    src/state/FatigueStateMachine.hpp
    src/vision/RawFrame.hpp
    src/vision/ILandmarkDetector.hpp
    src/vision/FaceLandmarker.hpp
    src/vision/ExternalLandmarks.hpp
    src/vision/OnnxFaceLandmarker.hpp
    src/vision/CameraService.hpp
    src/vision/VisionPipeline.hpp
    src/vision/drivers/ICameraDriver.hpp
    src/vision/drivers/SyntheticCameraDriver.hpp
    src/vision/drivers/WindowsMfCameraDriver.hpp
    src/vision/drivers/AndroidCamera2Driver.hpp
    src/vision/drivers/PixelConverters.hpp
    src/platform/PlatformLifecycleAdapter.hpp
    src/platform/windows/SystemTrayManager.hpp
    src/storage/DatabaseService.hpp
    src/storage/StudyDataLog.hpp
    src/study/StudyWorkflowTracker.hpp
    src/network/NetworkSyncWorker.hpp
    src/ui/FatigueFloatingIndicator.hpp
    src/engine/AsyncPipelineEngine.hpp
)

set(EFD_CORE_SOURCES
    src/analysis/FeatureExtractor.cpp
    src/analysis/EmdCalculator.cpp
    src/analysis/MseCalculator.cpp
    src/analysis/AdaptiveBaseline.cpp
    src/analysis/RollingOpenBaseline.cpp
    src/analysis/GazeTracker.cpp
    src/analysis/PersonalTrendBaseline.cpp
    src/state/FatigueStateMachine.cpp
    src/vision/FaceLandmarker.cpp
    src/vision/ExternalLandmarks.cpp
    src/vision/LandmarkDetectorFactory.cpp
    src/vision/CameraService.cpp
    src/vision/VisionPipeline.cpp
    src/vision/drivers/SyntheticCameraDriver.cpp
    src/platform/PlatformLifecycleAdapter.cpp
    src/storage/DatabaseService.cpp
    src/storage/StudyDataLog.cpp
    src/study/StudyWorkflowTracker.cpp
    src/network/NetworkSyncWorker.cpp
    src/engine/AsyncPipelineEngine.cpp
)

if(WIN32)
    list(APPEND EFD_CORE_SOURCES 
        src/vision/drivers/WindowsMfCameraDriver.cpp
        src/platform/windows/SystemTrayManager.cpp
        src/ui/FatigueFloatingIndicator.cpp
    )
endif()

if(EFD_HAS_ONNX)
    list(APPEND EFD_CORE_SOURCES src/vision/OnnxFaceLandmarker.cpp)
endif()

add_library(efd_core STATIC
    ${EFD_CORE_HEADERS}
    ${EFD_CORE_SOURCES}
)

if(WIN32)
    target_link_libraries(efd_core PUBLIC mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib gdi32 user32 shell32 gdiplus winhttp.lib)
endif()

target_include_directories(efd_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(efd_core PUBLIC cxx_std_20)

if(EFD_HAS_ONNX)
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        IMPORTED_IMPLIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
    target_link_libraries(efd_core PUBLIC onnxruntime)
    target_compile_definitions(efd_core PUBLIC EFD_HAS_ONNX)
endif()

# 執行檔旁放置 onnxruntime.dll 與 models/*.onnx：
# Windows 系統資料夾內建舊版 onnxruntime.dll，必須放在執行檔旁才會優先載入正確版本
function(efd_copy_model_runtime target)
    if(EFD_HAS_ONNX)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EFD_SOURCE_ROOT}/models/face_detector.onnx"
                "${EFD_SOURCE_ROOT}/models/face_landmarks.onnx"
                "$<TARGET_FILE_DIR:${target}>/models"
        )
    endif()
endfunction()

# -----------------------------------------------------------------------------
# CLI / Simulation App Target: efd_cli
# -----------------------------------------------------------------------------
if(EFD_BUILD_CLI)
    if(WIN32)
        add_executable(efd_cli
            src/main.cpp
            src/ui/NativeWelcomeWindow.cpp
            src/ui/NativeWelcomeWindow.hpp
            src/ui/FatigueFloatingIndicator.cpp
            src/ui/FatigueFloatingIndicator.hpp
            src/platform/windows/SystemTrayManager.cpp
            src/platform/windows/SystemTrayManager.hpp
            src/ui/Theme.hpp
        )
        target_link_libraries(efd_cli PRIVATE efd_core gdi32 user32 shell32 gdiplus)
        efd_copy_model_runtime(efd_cli)
    else()
        add_executable(efd_cli
            src/main.cpp
        )
        target_link_libraries(efd_cli PRIVATE efd_core)
    endif()
endif()

# -----------------------------------------------------------------------------
# GUI Application Target: efd_gui (Windows 歡迎 / 校準 / 監控介面)
# -----------------------------------------------------------------------------
if(WIN32)
    add_executable(efd_gui WIN32
        src/ui/main_gui.cpp
        src/ui/NativeWelcomeWindow.cpp
        src/ui/NativeWelcomeWindow.hpp
        src/ui/FatigueFloatingIndicator.cpp
        src/ui/FatigueFloatingIndicator.hpp
        src/platform/windows/SystemTrayManager.cpp
        src/platform/windows/SystemTrayManager.hpp
        src/platform/windows/app.rc
        src/ui/Theme.hpp
    )
    target_link_libraries(efd_gui PRIVATE efd_core gdi32 user32 shell32 gdiplus)
    add_custom_command(TARGET efd_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:efd_gui>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/design"
            "$<TARGET_FILE_DIR:efd_gui>/design"
    )
    efd_copy_model_runtime(efd_gui)
endif()

# -----------------------------------------------------------------------------
# Unit Tests Target
# -----------------------------------------------------------------------------
if(EFD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

