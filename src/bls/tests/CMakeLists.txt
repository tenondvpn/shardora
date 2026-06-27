cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

set(bls_test_src
    test_main.cc
    test_agg_bls.cc
    test_bls_sign.cc
    test_bls_utils.cc
    test_bls_utils_branches.cc
    test_bls_extra_branches.cc
    test_bls_function_branches.cc
    test_dkg_basic.cc)
add_executable(bls_test ${bls_test_src})

add_dependencies(bls_test bls)
target_link_libraries(bls_test PRIVATE bls network gtest gtest_main gmock gmock_main network
    consensus
    sync
    shardoravm
    block
    vss
    ck
    elect
    timeblock
    pools
    broadcast
    dht
    transport
    contract
    pki
    big_num
    db
    security
    common
    protos
    tcmalloc
    pthread
    gmp)
