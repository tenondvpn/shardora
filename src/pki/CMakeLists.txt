cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ pki_src)
add_library(pki STATIC ${pki_src})

target_link_libraries(pki PRIVATE gmssl pbc fmt ssl crypto log4cpp pthread dl)
