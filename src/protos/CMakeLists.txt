cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ protos_src)
add_library(protos STATIC ${protos_src})
target_link_libraries(protos PRIVATE protobuf)

