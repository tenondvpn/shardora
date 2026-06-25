# Transitive static libs for unit tests that link common/security (PRIVATE deps are not propagated).
function(shardora_link_common_test_crypto target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "shardora_link_common_test_crypto: target '${target}' does not exist")
    endif()

    set(_crypto_libs
        ssl
        crypto
        spdlog
        protobuf
        ethash
        xxhash
        keccak
        geolite2++
        maxminddb
        sodium
        oqs
        secp256k1)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        list(APPEND _crypto_libs gmssl)
    endif()

    target_link_libraries(${target} PRIVATE ${_crypto_libs})

    if(XENABLE_CODE_COVERAGE)
        target_link_libraries(${target} PRIVATE gcov)
    endif()
endfunction()
