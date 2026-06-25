cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

# Legacy sources (test_block_acceptor.cc, test_view_block_chain.cc, etc.) target removed
# APIs and protobuf field accessors; re-enable them here after updating to current code.
set(hotstuff_test_src
    test_hotstuff_placeholder.cc
    test_hotstuff_branches.cc)
add_executable(hotstuff_test ${hotstuff_test_src})

add_dependencies(hotstuff_test consensus)
# consensus -> elect -> block; block needs pools symbols (BlockManager, etc.)
target_link_libraries(
    hotstuff_test
    PRIVATE
    consensus
    block
    elect
    timeblock
    pools
    db
    protos
    common
    security
    gmp
    gmpxx
    gtest
    gtest_main
    gmock
    gmock_main
    pthread
    dl)
