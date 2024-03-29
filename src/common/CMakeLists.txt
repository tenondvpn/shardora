cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ common_src)
aux_source_directory(./tick tick_src)
add_library(common STATIC ${common_src} ${tick_src} )
target_link_libraries(common PRIVATE ssl crypto log4cpp ethash xxhash geolite2++ maxminddb)

