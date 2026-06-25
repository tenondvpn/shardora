cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ transport_src)
aux_source_directory(./proto proto_src)
add_library(transport STATIC ${transport_src} ${proto_src})

target_link_libraries(transport PRIVATE db protos common protobuf uv)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(transport PRIVATE tnet)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(transport PRIVATE dl)
endif()
