cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ db_test_src)
add_executable(db_test ${db_test_src})

add_dependencies(db_test db)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(db_test PRIVATE db common leveldb rocksdb gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(db_test PRIVATE db common leveldb bz2 gtest gtest_main gmock gmock_main)
endif()
