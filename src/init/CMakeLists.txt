cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ init_src)
add_library(init STATIC ${init_src})

target_link_libraries(init PRIVATE block network uv ssl crypto)
# Quoted: value is a comma-separated byte list; unquoted splits into invalid macros.
target_compile_definitions(init PRIVATE
    "REPLACE_WHITEBOX_PK=${REPLACE_WHITEBOX_PK}"
    "REPLACE_WHITEBOX_SK=${REPLACE_WHITEBOX_SK}"
)

if (XENABLE_CODE_COVERAGE)
    target_link_libraries(init PRIVATE gcov)
endif()
