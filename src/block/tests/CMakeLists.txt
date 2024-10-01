cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ block_test_src)
add_executable(block_test ${block_test_src})

add_dependencies(block_test block)
target_link_libraries(block_test PRIVATE block sync dbsvr contract db dht transport uv common ck gtest gtest_main gmock gmock_main pthread dl)
