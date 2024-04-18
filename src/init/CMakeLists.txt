cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ init_src)
if(CONSENSUS_HOTSTUFF)
    add_definitions(-DENABLE_HOTSTUFF=1)
endif()
add_library(init STATIC ${init_src})

target_link_libraries(init PRIVATE block network)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(init PRIVATE gcov)
endif()

