cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ sync_test_src)
add_executable(sync_test ${sync_test_src})

add_dependencies(sync_test sync)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sync_test PRIVATE sync common protos db gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(sync_test PRIVATE sync common protos db gtest gtest_main gmock gmock_main)
endif()
