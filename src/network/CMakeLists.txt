cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ network_src)
aux_source_directory(./proto proto_src)
add_library(network STATIC ${network_src} ${proto_src})

target_link_libraries(network PRIVATE broadcast dht)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(network PRIVATE gcov)
endif()

