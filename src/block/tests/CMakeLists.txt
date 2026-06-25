cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ block_test_src)
add_executable(block_test ${block_test_src})

add_dependencies(block_test block)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(block_test PRIVATE block common pools protos db gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(block_test PRIVATE block common pools protos db gtest gtest_main gmock gmock_main)
endif()
