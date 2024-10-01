cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ http_src)
add_library(http STATIC ${http_src})

target_link_libraries(http PRIVATE common evhtp event event_openssl ssl crypto log4cpp pthread dl)
