cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ vss_src)
add_library(vss STATIC ${vss_src})

target_link_libraries(vss PRIVATE security transport)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(vss PRIVATE gcov)
endif()

