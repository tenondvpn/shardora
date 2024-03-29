cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ init_src)
add_library(init STATIC ${init_src})

target_link_libraries(init PRIVATE block network)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(init PRIVATE gcov)
endif()

