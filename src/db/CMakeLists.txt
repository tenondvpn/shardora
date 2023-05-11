cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ db_src)
add_library(db STATIC ${db_src}  "../protos/prefix_db.h")

target_link_libraries(db PRIVATE rocksdb log4cpp snappy z bz2 zstd lz4)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(db PRIVATE gcov)
endif()

