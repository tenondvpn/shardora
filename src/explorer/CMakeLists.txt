cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ explorer_src)
add_library(explorer STATIC ${explorer_src})

target_link_libraries(explorer PRIVATE
    common
    block
    protos
    ${CMAKE_SOURCE_DIR}/third_party/lib/libsqlite3.a
)

target_include_directories(explorer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/include
)
