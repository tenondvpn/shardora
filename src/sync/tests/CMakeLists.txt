cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ sync_test_src)
add_executable(sync_test ${sync_test_src})

add_dependencies(sync_test sync)
target_link_libraries(sync_test PRIVATE sync common gtest gtest_main gmock gmock_main pthread dl)
