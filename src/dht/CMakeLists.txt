cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ dht_src)
aux_source_directory(./proto proto_src)
add_library(dht STATIC ${dht_src} ${proto_src})

target_link_libraries(dht PRIVATE transport)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(dht PRIVATE gcov)
endif()

