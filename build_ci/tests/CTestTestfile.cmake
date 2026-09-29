# CMake generated Testfile for 
# Source directory: /__w/dmsdio/dmsdio/tests
# Build directory: /__w/dmsdio/dmsdio/build_ci/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_dmsdio "/usr/bin/cmake" "-E" "env" "DMOD_DMF_DIR=/__w/dmsdio/dmsdio/build_ci/dmf" "bash" "-c" "/usr/local/bin/dmf-get install -d /__w/dmsdio/dmsdio/build_ci/dmf/test_dmsdio-local.dmd -y && /usr/local/bin/dmod_loader /__w/dmsdio/dmsdio/build_ci/dmf/test_dmsdio.dmf")
set_tests_properties(test_dmsdio PROPERTIES  _BACKTRACE_TRIPLES "/__w/dmsdio/dmsdio/tests/CMakeLists.txt;36;add_test;/__w/dmsdio/dmsdio/tests/CMakeLists.txt;0;")
