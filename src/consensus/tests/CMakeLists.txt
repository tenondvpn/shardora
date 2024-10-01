cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ consensus_test_src)
add_executable(consensus_test ${consensus_test_src})

add_dependencies(consensus_test consensus)
target_link_libraries(consensus_test PRIVATE consensus gtest gtest_main gmock gmock_main pthread dl)
