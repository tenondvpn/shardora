cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ websocket_src)
add_library(websocket ${websocket_src} "websocket_client.h")

target_link_libraries(websocket PRIVATE common boost_system boost_thread boost_atomic ssl crypto log4cpp pthread dl)
