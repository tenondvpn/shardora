cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ bls_test_src)
add_executable(bls_test ${bls_test_src})

add_dependencies(bls_test bls)
find_package(OpenSSL REQUIRED)
target_link_libraries(bls_test PRIVATE bls network gtest gtest_main gmock gmock_main pthread dl OpenSSL::SSL OpenSSL::Crypto)
