cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ consensus_src)
aux_source_directory(./zbft zbft_src)
aux_source_directory(./hotstuff hotstuff_src)
add_library(consensus STATIC ${consensus_src} ${zbft_src} ${hotstuff_src})

target_link_libraries(consensus PRIVATE timeblock zjcvm elect pools)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(consensus PRIVATE gcov)
endif()

