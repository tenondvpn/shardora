cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ vss_test_src)
add_executable(vss_test ${vss_test_src})

add_dependencies(vss_test vss)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(vss_test PRIVATE vss bz2 gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(vss_test PRIVATE vss gtest gtest_main gmock gmock_main)
endif()
