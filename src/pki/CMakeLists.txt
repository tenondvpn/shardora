cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ pki_src)
add_library(pki STATIC ${pki_src})

target_link_libraries(pki PRIVATE gmssl pbc gmp fmt ssl crypto pthread)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(pki PRIVATE dl)
endif()
