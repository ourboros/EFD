add_executable(efd_tests
    test_features.cpp
)

target_link_libraries(efd_tests PRIVATE efd_core)
# 測試影像 (MediaPipe 官方公開測試照片 portrait.jpg 縮成寬 480 的 PPM)
target_compile_definitions(efd_tests PRIVATE EFD_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
efd_copy_model_runtime(efd_tests)

add_test(NAME CoreAlgorithmTests COMMAND efd_tests)

# 診斷重播工具：重播手機診斷模式匯出的畫面 (不列入自動測試)
add_executable(efd_diag_replay
    diag_replay.cpp
)

target_link_libraries(efd_diag_replay PRIVATE efd_core)
efd_copy_model_runtime(efd_diag_replay)
