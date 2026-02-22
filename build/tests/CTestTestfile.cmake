# CMake generated Testfile for 
# Source directory: /home/arun/projects/nuc_display/tests
# Build directory: /home/arun/projects/nuc_display/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[UtilsTests]=] "/home/arun/projects/nuc_display/build/tests/test_utils")
set_tests_properties([=[UtilsTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/arun/projects/nuc_display/tests/CMakeLists.txt;20;add_test;/home/arun/projects/nuc_display/tests/CMakeLists.txt;0;")
add_test([=[ModuleTests]=] "/home/arun/projects/nuc_display/build/tests/test_modules")
set_tests_properties([=[ModuleTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/arun/projects/nuc_display/tests/CMakeLists.txt;34;add_test;/home/arun/projects/nuc_display/tests/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
