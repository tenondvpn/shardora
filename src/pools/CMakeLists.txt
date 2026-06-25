cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ pools_src)
add_library(pools STATIC ${pools_src})

target_link_libraries(pools PRIVATE sync network security)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(pools PRIVATE gcov)
    # Enables ShardStatistic::Init synthetic tag + TxPoolManager::AddTx in headers for coverage runs only.
    target_compile_definitions(pools PRIVATE SHARDORA_UNITTEST)
endif()

option(BUILD_POOLS_INTEGRATION_TEST
    "Build pools_integration_test (full LINK_ARGS link; see src/pools/tests_integration/README.txt)"
    ON)
if(BUILD_POOLS_INTEGRATION_TEST)
    add_subdirectory(tests_integration)
endif()
