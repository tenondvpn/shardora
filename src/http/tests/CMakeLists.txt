cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ http_test_src)
add_executable(http_test ${http_test_src})

add_dependencies(http_test http)
target_link_libraries(http_test PRIVATE http gtest gtest_main gmock gmock_main pthread dl)
