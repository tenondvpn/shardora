cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ broadcast_test_src)
add_executable(broadcast_test ${broadcast_test_src})

add_dependencies(broadcast_test broadcast)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(broadcast_test PRIVATE broadcast dht transport uv common gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(broadcast_test PRIVATE broadcast gtest gtest_main gmock gmock_main)
endif()
