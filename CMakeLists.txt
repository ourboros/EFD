cmake_minimum_required(VERSION 3.20)

project(EFD 
    VERSION 1.0.0 
    DESCRIPTION "Eye Fatigue Detection Native System"
    LANGUAGES CXX C
)

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
    src/state/FatigueStateMachine.hpp
    src/vision/RawFrame.hpp
    src/vision/FaceLandmarker.hpp
    src/vision/CameraService.hpp
    src/vision/VisionPipeline.hpp
    src/vision/drivers/ICameraDriver.hpp
    src/vision/drivers/SyntheticCameraDriver.hpp
    src/vision/drivers/WindowsMfCameraDriver.hpp
    src/vision/drivers/AndroidCamera2Driver.hpp
    src/vision/drivers/AppleAvFoundationCameraDriver.hpp
    src/platform/PlatformLifecycleAdapter.hpp
    src/platform/windows/SystemTrayManager.hpp
    src/storage/DatabaseService.hpp
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
    src/state/FatigueStateMachine.cpp
    src/vision/FaceLandmarker.cpp
    src/vision/CameraService.cpp
    src/vision/VisionPipeline.cpp
    src/vision/drivers/SyntheticCameraDriver.cpp
    src/platform/PlatformLifecycleAdapter.cpp
    src/storage/DatabaseService.cpp
    src/study/StudyWorkflowTracker.cpp
    src/network/NetworkSyncWorker.cpp
    src/engine/AsyncPipelineEngine.cpp
)

if(WIN32)
    list(APPEND EFD_CORE_SOURCES src/vision/drivers/WindowsMfCameraDriver.cpp)
    list(APPEND EFD_CORE_SOURCES 
        src/vision/drivers/WindowsMfCameraDriver.cpp
        src/platform/windows/SystemTrayManager.cpp
        src/ui/FatigueFloatingIndicator.cpp
    )
endif()

add_library(efd_core STATIC
    ${EFD_CORE_HEADERS}
    ${EFD_CORE_SOURCES}
)

if(WIN32)
    target_link_libraries(efd_core PUBLIC mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib)
    target_link_libraries(efd_core PUBLIC mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib gdi32 user32 shell32 gdiplus)
endif()

target_include_directories(efd_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(efd_core PUBLIC cxx_std_20)

# -----------------------------------------------------------------------------
# CLI / Simulation App Target: efd_cli
# -----------------------------------------------------------------------------
if(EFD_BUILD_CLI)
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
    target_link_libraries(efd_cli PRIVATE efd_core)
    if(WIN32)
        target_link_libraries(efd_cli PRIVATE gdi32 user32 shell32 gdiplus)
    endif()
endif()

# -----------------------------------------------------------------------------
# GUI Application Target: efd_gui (Full Welcome / Calibration / Monitoring UI)
# -----------------------------------------------------------------------------
set(EFD_GUI_SOURCES
    src/ui/main_gui.cpp
    src/ui/NativeWelcomeWindow.cpp
    src/ui/NativeWelcomeWindow.hpp
    src/ui/FatigueFloatingIndicator.cpp
    src/ui/FatigueFloatingIndicator.hpp
    src/platform/windows/SystemTrayManager.cpp
    src/platform/windows/SystemTrayManager.hpp
    src/ui/Theme.hpp
)

add_executable(efd_gui
    ${EFD_GUI_SOURCES}
)

target_link_libraries(efd_gui PRIVATE efd_core)

if(WIN32)
    target_link_libraries(efd_gui PRIVATE gdi32 user32 shell32 gdiplus)
    add_custom_command(TARGET efd_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:efd_gui>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/design"
            "$<TARGET_FILE_DIR:efd_gui>/design"
    )
elseif(APPLE AND NOT IOS)
    set_target_properties(efd_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME "EFD"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.efd.EyeFatigueDetection"
    )
    target_link_libraries(efd_gui PRIVATE "-framework Foundation" "-framework AVFoundation" "-framework AppKit")
endif()

# -----------------------------------------------------------------------------
# iOS Application Target: EFD_iOS
# -----------------------------------------------------------------------------
if(IOS)
    add_executable(EFD_iOS
        ios/main.m
        ios/AppDelegate.h
        ios/AppDelegate.mm
        ios/ViewController.h
        ios/ViewController.mm
    )
    target_link_libraries(EFD_iOS PRIVATE efd_core "-framework UIKit" "-framework Foundation" "-framework AVFoundation" "-framework AudioToolbox")
    set_target_properties(EFD_iOS PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.efd.EyeFatigueDetection"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )
endif()

# -----------------------------------------------------------------------------
# Unit Tests Target
# -----------------------------------------------------------------------------
if(EFD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

