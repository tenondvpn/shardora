cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ timeblock_src)
add_library(timeblock STATIC ${timeblock_src} )

target_link_libraries(timeblock PRIVATE consensus pools protos)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(timeblock PRIVATE gcov)
endif()

