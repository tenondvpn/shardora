cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ block_src)
add_library(block STATIC ${block_src})

target_link_libraries(block PRIVATE db)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(block PRIVATE db gcov)
endif()

