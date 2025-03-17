cmake_minimum_required(VERSION 3.8)

aux_source_directory(./ security_src)
aux_source_directory(./ecdsa ecdsa_src)
aux_source_directory(./gmssl gmssl_src)
add_library(security STATIC ${gmssl_src} ${ecdsa_src} ${security_src} )

target_link_libraries(security PRIVATE common ssl crypto secp256k1 gmssl)
if (XENABLE_CODE_COVERAGE)
    target_link_libraries(security PRIVATE gcov)
endif()
