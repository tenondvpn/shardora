cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ pools_test_src)
add_executable(pools_test ${pools_test_src})

add_dependencies(pools_test pools)
target_link_libraries(pools_test PRIVATE pools gtest gtest_main gmock gmock_main pthread dl)
