cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ hotstuff_test_src)
add_executable(hotstuff_test ${hotstuff_test_src})

add_dependencies(hotstuff_test consensus)
target_link_libraries(hotstuff_test PRIVATE consensus gmp gmpxx gtest gtest_main ../../../../third_party/depends/libs/linux/libgmock.a ../../../../third_party/depends/libs/linux/libgmock_main.a pthread dl)
