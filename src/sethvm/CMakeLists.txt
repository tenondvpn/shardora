cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ shardoravm_src)
add_library(shardoravm STATIC ${shardoravm_src})

target_link_libraries(shardoravm PRIVATE contract common evmone evmc-loader evmc-instructions)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(shardoravm PRIVATE gcov)
endif()

