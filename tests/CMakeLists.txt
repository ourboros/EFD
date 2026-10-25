add_executable(efd_tests
    test_features.cpp
)

target_link_libraries(efd_tests PRIVATE efd_core)

add_test(NAME CoreAlgorithmTests COMMAND efd_tests)


# 測試依賴 assert：Release 組態也必須保留斷言
if(MSVC)
    target_compile_options(efd_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(efd_tests PRIVATE -UNDEBUG)
endif()
