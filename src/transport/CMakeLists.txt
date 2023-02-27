cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ transport_src)
aux_source_directory(./proto proto_src)
add_library(transport STATIC ${transport_src} ${proto_src})

target_link_libraries(transport PRIVATE security db tnet protos common protobuf dl)

