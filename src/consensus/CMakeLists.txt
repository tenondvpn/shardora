cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ consensus_src)
aux_source_directory(./zbft zbft_src)
add_library(consensus STATIC ${consensus_src} ${zbft_src})

target_link_libraries(consensus PRIVATE timeblock zjcvm pools)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(consensus PRIVATE gcov)
endif()

