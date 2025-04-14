cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ zjcvm_src)
add_library(zjcvm STATIC ${zjcvm_src})

target_link_libraries(zjcvm PRIVATE contract common evmone hex evmc-loader evmc-instructions)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(zjcvm PRIVATE gcov)
endif()

