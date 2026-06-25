cmake_minimum_required(VERSION 3.22)

# WebSocket++ headers: prefer third_party/include (same layout as global include path).
set(_WEBSOCKETPP_VENDOR_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/include")
if(EXISTS "${_WEBSOCKETPP_VENDOR_INCLUDE}/websocketpp/common/thread.hpp")
    set(_WEBSOCKETPP_INCLUDE_DIR "${_WEBSOCKETPP_VENDOR_INCLUDE}")
else()
    include(FetchContent)
    # Boost 1.87+ removed io_service, resolver::query/iterator, timer::expires_from_now, address_v4::from_string.
    # PR #1164 fix (io_context migration) is not merged on zaphoyd/develop; the SHA exists on this fork's master.
    FetchContent_Declare(
        websocketpp
        GIT_REPOSITORY https://github.com/amini-allight/websocketpp.git
        GIT_TAG 363ec80247782e4b933f93079f02a0835de28570
    )
    FetchContent_GetProperties(websocketpp)
    if(NOT websocketpp_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(websocketpp)
    endif()
    set(_WEBSOCKETPP_INCLUDE_DIR "${websocketpp_SOURCE_DIR}")
endif()

aux_source_directory(./ websocket_src)
add_library(websocket ${websocket_src} "websocket_client.h")

# WebSocket++ 0.8-era headers (incl. PR #1164 forks) are not C++20-clean; compile this target as C++17.
set_target_properties(websocket PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(websocket PUBLIC "${_WEBSOCKETPP_INCLUDE_DIR}")
target_link_libraries(websocket PRIVATE common boost_system boost_thread boost_atomic ssl crypto pthread)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(websocket PRIVATE dl)
endif()
if(WIN32)
    target_link_libraries(websocket PRIVATE ws2_32)
endif()
