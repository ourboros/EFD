add_executable(efd_tests
    test_features.cpp
)

target_link_libraries(efd_tests PRIVATE efd_core)

add_test(NAME CoreAlgorithmTests COMMAND efd_tests)

