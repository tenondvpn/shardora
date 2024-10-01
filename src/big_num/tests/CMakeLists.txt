cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ bignum_test_src)
add_executable(bignum_test ${bignum_test_src})

add_dependencies(bignum_test big_num)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(bignum_test PRIVATE big_num gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(bignum_test PRIVATE big_num gtest gtest_main gmock gmock_main)
endif()
