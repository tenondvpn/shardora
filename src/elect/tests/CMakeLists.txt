cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ elect_test_src)
add_executable(elect_test ${elect_test_src})

add_dependencies(elect_test elect)
target_link_libraries(elect_test PRIVATE elect gtest gtest_main gmock gmock_main pthread dl)
