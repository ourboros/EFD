# CMake generated Testfile for 
# Source directory: E:/Project/EFD/tests
# Build directory: E:/Project/EFD/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[CoreAlgorithmTests]=] "E:/Project/EFD/build/tests/Debug/efd_tests.exe")
  set_tests_properties([=[CoreAlgorithmTests]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/Project/EFD/tests/CMakeLists.txt;7;add_test;E:/Project/EFD/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[CoreAlgorithmTests]=] "E:/Project/EFD/build/tests/Release/efd_tests.exe")
  set_tests_properties([=[CoreAlgorithmTests]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/Project/EFD/tests/CMakeLists.txt;7;add_test;E:/Project/EFD/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[CoreAlgorithmTests]=] "E:/Project/EFD/build/tests/MinSizeRel/efd_tests.exe")
  set_tests_properties([=[CoreAlgorithmTests]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/Project/EFD/tests/CMakeLists.txt;7;add_test;E:/Project/EFD/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[CoreAlgorithmTests]=] "E:/Project/EFD/build/tests/RelWithDebInfo/efd_tests.exe")
  set_tests_properties([=[CoreAlgorithmTests]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/Project/EFD/tests/CMakeLists.txt;7;add_test;E:/Project/EFD/tests/CMakeLists.txt;0;")
else()
  add_test([=[CoreAlgorithmTests]=] NOT_AVAILABLE)
endif()
