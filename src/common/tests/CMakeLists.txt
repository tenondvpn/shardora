cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ common_test_src)
add_executable(common_test ${common_test_src})

add_dependencies(common_test common)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(common_test PRIVATE common gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(common_test PRIVATE common gtest gtest_main gmock gmock_main)
endif()
