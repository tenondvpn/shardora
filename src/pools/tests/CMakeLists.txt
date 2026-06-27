cmake_minimum_required(VERSION 3.22)

add_compile_options(-Wno-unused-parameter)

# Include tx_utils / struct branch tests (gcov maps pools_test → src/pools).
set(pools_test_src
    test_height_tree_level.cc
    test_leaf_height_tree.cc
    test_pools_tx_utils_branches.cc
    test_unique_hash_lru_set_branches.cc
    test_pools_extra_branches.cc
    test_leaf_height_tree_extra.cc
    test_height_tree_extra.cc
    test_cross_pool.cc
    test_tx_pool_inline.cc
    test_tx_pool_methods.cc
    test_tx_pool_tx.cc
    test_tx_pool_addtx.cc
    test_to_txs_pools.cc
    test_cross_block_manager.cc
    test_tx_pool_extra.cc
    test_to_txs_pools_extra.cc
    test_pools_stubs.cc
    test_smoke.cc
    test_shard_statistic_init.cc
    test_shard_statistic_extra.cc
    test_account_qps_lru_map_extra.cc
    test_tx_pool_manager.cc
    test_tx_pool_extra3.cc
    test_shard_statistic_extra3.cc
    test_shard_statistic_extra4.cc
    test_to_txs_pools_extra2.cc
    test_tx_pool_manager_extra.cc
    test_tx_pool_extra4.cc
    test_to_txs_pools_extra3.cc
    test_tx_pool_manager_extra2.cc
    test_tx_pool_manager_extra3.cc
    test_tx_pool_manager_extra4.cc
    test_pools_branch_extra.cc
    test_root_cross_pool_enhanced.cc
    test_cross_pool_enhanced.cc
    test_pools_coverage_enhancement.cc
    test_pools_boundary_conditions.cc
    test_to_confirm_latency_tracker.cc
    pools_test_ck_link_stub.cc)
add_executable(pools_test ${pools_test_src})

if (XENABLE_CODE_COVERAGE)
    target_compile_definitions(pools_test PRIVATE SHARDORA_UNITTEST)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(pools_test PRIVATE pools db protos common gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(pools_test PRIVATE pools db protos common gtest gtest_main gmock gmock_main)
endif()
