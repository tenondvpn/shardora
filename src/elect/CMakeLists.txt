cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ elect_src)
add_library(elect STATIC ${elect_src})

target_link_libraries(elect PRIVATE block bls security network)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(elect PRIVATE gcov)
endif()

