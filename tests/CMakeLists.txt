add_executable(efd_tests
    test_features.cpp
)

target_link_libraries(efd_tests PRIVATE efd_core)

add_test(NAME CoreAlgorithmTests COMMAND efd_tests)

# 診斷重播工具：重播 iOS 診斷模式匯出的畫面 (不列入自動測試)
add_executable(efd_diag_replay
    diag_replay.cpp
)

target_link_libraries(efd_diag_replay PRIVATE efd_core)

