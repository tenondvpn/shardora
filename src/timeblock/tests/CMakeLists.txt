cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ tmblock_test_src)
add_executable(tmblock_test ${tmblock_test_src})

add_dependencies(tmblock_test timeblock)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(tmblock_test PRIVATE timeblock bls common protos db ssl crypto gmp gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(tmblock_test PRIVATE timeblock common protos db gtest gtest_main gmock gmock_main)
endif()
