cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ contract_src)
aux_source_directory(./proto proto_src)
add_library(contract STATIC ${contract_src} ${proto_src})

target_link_libraries(contract PRIVATE shardoravm protos security db big_num PBC pbc gmp)

