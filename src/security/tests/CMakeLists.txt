cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ security_test_src)
add_executable(security_test ${security_test_src})

add_dependencies(security_test security)
target_link_libraries(security_test PRIVATE security gtest gtest_main gmock gmock_main pthread dl)
