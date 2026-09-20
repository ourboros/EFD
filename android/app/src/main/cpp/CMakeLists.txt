cmake_minimum_required(VERSION 3.20)
project(efd_jni CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 專案原始碼目錄 (引用根目錄 src 與 include)
set(EFD_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

include_directories(
    ${EFD_ROOT_DIR}/include
    ${EFD_ROOT_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(efd_jni SHARED
    native-lib.cpp
    ${EFD_ROOT_DIR}/src/analysis/FeatureExtractor.cpp
    ${EFD_ROOT_DIR}/src/analysis/EmdCalculator.cpp
    ${EFD_ROOT_DIR}/src/analysis/MseCalculator.cpp
    ${EFD_ROOT_DIR}/src/analysis/AdaptiveBaseline.cpp
    ${EFD_ROOT_DIR}/src/state/FatigueStateMachine.cpp
    ${EFD_ROOT_DIR}/src/vision/FaceLandmarker.cpp
    ${EFD_ROOT_DIR}/src/vision/CameraService.cpp
    ${EFD_ROOT_DIR}/src/vision/VisionPipeline.cpp
    ${EFD_ROOT_DIR}/src/vision/drivers/SyntheticCameraDriver.cpp
    ${EFD_ROOT_DIR}/src/platform/PlatformLifecycleAdapter.cpp
    ${EFD_ROOT_DIR}/src/storage/DatabaseService.cpp
    ${EFD_ROOT_DIR}/src/study/StudyWorkflowTracker.cpp
    ${EFD_ROOT_DIR}/src/network/NetworkSyncWorker.cpp
    ${EFD_ROOT_DIR}/src/engine/AsyncPipelineEngine.cpp
)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(efd_jni
    ${log-lib}
    ${android-lib}
)

