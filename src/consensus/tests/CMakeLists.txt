cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

# Keep heavy integration tests (e.g. test_tx.cc) out of the default target; they are
# buildable manually when needed. Branch-coverage gtests are included here.
set(consensus_test_src
    test_consensus_utils.cc
    test_consensus_hotstuff_zbft_surface.cc
    test_hotstuff_utils_branches.cc
    test_consensus_function_branches.cc
    test_consensus_function_coverage_100.cc
    test_consensus_coverage_enhancement.cc
    test_consensus_check_transaction_valid.cc
    test_smoke.cc)
add_executable(consensus_test ${consensus_test_src})

target_link_libraries(
    consensus_test
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
    gtest
    gtest_main
    gmock
    gmock_main
    pthread
    dl)
