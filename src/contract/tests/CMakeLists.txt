cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

# All *.cc tests (branch runners + legacy smoke). Custom test_main.cc removed:
# we link gtest_main for ::testing::InitGoogleTest / RUN_ALL_TESTS.
file(GLOB CONTRACT_TEST_CC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
list(FILTER CONTRACT_TEST_CC EXCLUDE REGEX "test_main\\.cc$")
add_executable(contract_test ${CONTRACT_TEST_CC})

target_link_libraries(contract_test PRIVATE contract security common gtest gtest_main gmock gmock_main pthread dl)
