cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ tmblock_test_src)
add_executable(tmblock_test ${tmblock_test_src})

add_dependencies(tmblock_test tmblock)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(tmblock_test PRIVATE timeblock tvm contract common bz2 gtest gtest_main gmock gmock_main pthread dl common)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(tmblock_test PRIVATE timeblock gtest gtest_main gmock gmock_main)
endif()
