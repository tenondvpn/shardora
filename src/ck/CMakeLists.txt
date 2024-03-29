cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ ck_src)
add_library(ck STATIC ${ck_src})

target_link_libraries(ck PRIVATE common clickhouse cityhash lz4)

