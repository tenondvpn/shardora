cmake_minimum_required(VERSION 3.22)

aux_source_directory(./ explorer_src)
add_library(explorer STATIC ${explorer_src})

find_package(SQLite3 REQUIRED)

target_link_libraries(explorer PRIVATE
    common
    block
    protos
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)

target_include_directories(explorer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SQLite3_INCLUDE_DIRS}
)
