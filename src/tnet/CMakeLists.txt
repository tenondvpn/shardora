cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ tnet_src)
aux_source_directory(./event event_src)
aux_source_directory(./socket socket_src)
aux_source_directory(./utils utils_src)
add_library(tnet STATIC ${tnet_src} ${event_src} ${socket_src} ${utils_src})

target_link_libraries(tnet PRIVATE common)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(tnet PRIVATE gcov)
endif()

