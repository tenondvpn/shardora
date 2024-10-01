cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ bls_src)
add_library(bls STATIC ${bls_src})

target_link_libraries(bls PRIVATE network security db dkgbls ff gmp gmpxx)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(bls PRIVATE gcov)
endif()

