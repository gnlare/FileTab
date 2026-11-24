# CMake generated Testfile for 
# Source directory: C:/Dev/3_Tools/FileTab/FileTab-cpp
# Build directory: C:/Dev/3_Tools/FileTab/FileTab-cpp/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[core_tests]=] "C:/Dev/3_Tools/FileTab/FileTab-cpp/build/Debug/core_tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;56;add_test;C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[core_tests]=] "C:/Dev/3_Tools/FileTab/FileTab-cpp/build/Release/core_tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;56;add_test;C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[core_tests]=] "C:/Dev/3_Tools/FileTab/FileTab-cpp/build/MinSizeRel/core_tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;56;add_test;C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[core_tests]=] "C:/Dev/3_Tools/FileTab/FileTab-cpp/build/RelWithDebInfo/core_tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;56;add_test;C:/Dev/3_Tools/FileTab/FileTab-cpp/CMakeLists.txt;0;")
else()
  add_test([=[core_tests]=] NOT_AVAILABLE)
endif()
