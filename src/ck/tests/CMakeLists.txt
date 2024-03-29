cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ ck_test_src)
add_executable(ck_test ${ck_test_src})

add_dependencies(ck_test ck)
target_link_libraries(ck_test PRIVATE ck gtest gtest_main gmock gmock_main pthread dl)
