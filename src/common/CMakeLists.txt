cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ common_src)
aux_source_directory(./tick tick_src)
add_library(common STATIC ${common_src} ${tick_src} )
target_link_libraries(common PRIVATE ssl  protobuf crypto spdlog ethash xxhash keccak geolite2++ maxminddb)
# Hash::sm3 uses libgmssl (GmSSL); keep next to common on consumers' link lines (Linux/Android third_party).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(common PRIVATE gmssl)
endif()

