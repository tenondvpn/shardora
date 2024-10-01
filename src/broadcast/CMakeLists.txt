cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ broadcast_src)
aux_source_directory(./proto proto_src)
add_library(broadcast STATIC ${broadcast_src} ${proto_src})

target_link_libraries(broadcast PRIVATE dht)

