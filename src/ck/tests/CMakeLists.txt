cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

# Header-only / light ck_utils coverage (table names, structs). Heavy client tests stay separate.
set(ck_test_src
    test_smoke.cc
    test_ck_utils_branches.cc)
add_executable(ck_test ${ck_test_src})

add_dependencies(ck_test ck)
target_link_libraries(ck_test PRIVATE ck gtest gtest_main gmock gmock_main pthread dl)
