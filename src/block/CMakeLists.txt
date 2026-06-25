cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ block_src)
add_library(block STATIC ${block_src})

# PUBLIC pools: libblock.a leaves unresolved refs to shardora::pools::*; consumers must link pools.
target_link_libraries(block PUBLIC pools PRIVATE db)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(block PRIVATE db gcov)
endif()

