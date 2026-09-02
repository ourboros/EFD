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
)

set(EFD_CORE_SOURCES
    src/analysis/FeatureExtractor.cpp
    src/analysis/EmdCalculator.cpp
    src/analysis/MseCalculator.cpp
    src/analysis/AdaptiveBaseline.cpp
    src/state/FatigueStateMachine.cpp
)

add_library(efd_core STATIC
    ${EFD_CORE_HEADERS}
    ${EFD_CORE_SOURCES}
)

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
    )
    target_link_libraries(efd_cli PRIVATE efd_core)
endif()

# -----------------------------------------------------------------------------
# Unit Tests Target
# -----------------------------------------------------------------------------
if(EFD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

