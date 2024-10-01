cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ transport_test_src)
add_executable(transport_test ${transport_test_src})

add_dependencies(transport_test transport)
target_link_libraries(transport_test PRIVATE transport event gtest gtest_main gmock gmock_main pthread dl)
