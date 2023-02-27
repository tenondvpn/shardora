cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ big_num_src)
add_library(big_num STATIC ${big_num_src} )

target_link_libraries(big_num PRIVATE common ssl ff gmp)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(big_num PRIVATE gcov)
endif()

