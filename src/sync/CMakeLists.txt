cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ sync_src)
add_library(sync STATIC ${sync_src})

target_link_libraries(sync PRIVATE dht transport common)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(sync PRIVATE gcov)
endif()

