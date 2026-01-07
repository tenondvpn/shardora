# option
option(BUILD_SHARED_LIBS "build shared library(.so/.a)" OFF)
option(BUILD_CLIENT "build shardora client" OFF)

# version required
cmake_minimum_required(VERSION 3.24.0)
project(shardora)

# =========================================================
# 1. Default Build Type to Release (Performance First)
# =========================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected, defaulting to Release for performance.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# =========================================================
# 2. Enable LTO (Link Time Optimization) - Critical for Performance
# =========================================================
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
if(ipo_supported)
    message(STATUS "IPO/LTO is supported. Enabling Interprocedural Optimization.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(WARNING "IPO/LTO is not supported: ${ipo_output}")
endif()

if(NOT BUILD_CLIENT)
    if(MSVC)
        set(CURL_LIBRARIES libcurl)
        set(CURL_INCLUDE_DIRS )
    else()
        #find_package(CURL REQUIRED)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(BUILD_SHARED_LIBS)
    set(shared_str "shared")
else()
    set(shared_str "static")
endif()

message(STATUS "Build Info: ${PROJECT_NAME} | System: ${CMAKE_SYSTEM_NAME} | Type: ${CMAKE_BUILD_TYPE} | Lib: ${shared_str}")

# path definitions
set(DEP_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(MAIN_DIR ${PROJECT_SOURCE_DIR}/src/main)

set(LINK_RT rt)
set(LINK_PTHREAD pthread)
set(LINK_ANDROID_LOG )
set(LINK_UUID )
set(LINK_YAML )

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIB_DIR
        ${DEP_DIR}/lib
        ${DEP_DIR}/lib64
        ${DEP_DIR}/libbls/deps/deps_inst/x86_or_x64/lib/)
    set(LINK_UUID uuid)
    if(DEFINED OPENSSL_INCLUDE_DIR)
        include_directories(${OPENSSL_INCLUDE_DIR})
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(LIB_DIR ${DEP_DIR}/libs/android)
    set(LINK_RT )
    set(LINK_PTHREAD )
    set(LINK_ANDROID_LOG log)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_compile_options(-Wno-c++11-narrowing)
    set(LINK_RT )
    if(NOT DEFINED IOS_PLATFORM)
        set(LIB_DIR ${DEP_DIR}/libs/mac)
    else() # iOS
        message(STATUS "IOS_PLATFORM=${IOS_PLATFORM}")
        set(LIB_DIR ${DEP_DIR}/libs/ios)
    endif()
    find_package(OpenSSL REQUIRED)
    include_directories(${OPENSSL_INCLUDE_DIR})
    find_library(CFLIB CoreFoundation)
    add_definitions(-DGUID_CFUUID)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LINK_RT )
    set(LINK_PTHREAD )
    set(LIB_DIR ${DEP_DIR}/libs/win)
else()
    message(FATAL_ERROR "unsupported CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
endif()

# include definitions
if(MSVC)
    include_directories(SYSTEM
        ${DEP_DIR}/include_win
    )
endif()
include_directories(SYSTEM
    ${DEP_DIR}/include
    ${DEP_DIR}/include/orocos
    ${DEP_DIR}/include/maxmind
    ${DEP_DIR}/include/libbls
    ${DEP_DIR}/include/geoip
    ${DEP_DIR}/include/uv
    ${DEP_DIR}/include/sslib
    ${DEP_DIR}/libbls/deps/deps_inst/x86_or_x64/include/
    ${CURL_INCLUDE_DIRS}
)

# compile definitions
add_definitions(
    -DTEST_LOCAL_NETWORK
    -DOPENSSL_ROOT_DIR=/usr/local/Cellar/openssl/1.0.2r
    -DENABLE_HOTSTUFF
    -DDISABLE_GENESIS_BLS_VERIFY

    #    -DSHARDORA_UNITTEST # for testing
    #-DFOR_CONSOLE_DEBUG
#    -DTEST_TRANSPORT_PERFOMANCE
#    -DSHARDORA_TRACE_MESSAGE
#    -DLEVELDB
#    -DCLIENT_USE_UV
#    -DMAKE_CLIENT_LIB
    #-DARMEABI_V7A
    #-DLATENCY
    #-DHOTSTUFF_TEST
    #-DUSE_SERVER_TEST_TRANSACTION
    #-DTEST_FORKING_ATTACK
    #-DMOCK_VERIFY
    #-DMOCK_SIGN
    #-DTEST_NO_CROSS
    #-DUSE_AGG_BLS

)

# =========================================================
# 3. Compiler Performance Optimizations
# =========================================================
if(MSVC)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/Zc:__cplusplus)
    add_compile_options(/utf-8)
    
    # MSVC Release Optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/Ox /Ob2 /Oi /Ot /GL)
        add_link_options(/LTCG)
    endif()
    
    set(LINK_DL )
else()
    # General Options
    add_compile_options(-Wall -pthread -fPIC)
    add_compile_options(-Wno-deprecated-declarations)
    add_compile_options(-Wno-unused-variable)
    
    # Extreme Performance Optimization for Release Build
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        # -O3: Highest optimization level
        # -march=native: Use all instruction sets available on the host CPU (AVX2/AVX512 etc). 
        #                Huge performance boost for BLS/Crypto.
        # -funroll-loops: Aggressive loop unrolling
	#add_compile_options(-O3 -march=native -funroll-loops)
	add_compile_options(-O3 -funroll-loops)
        # Enable Dead Code Elimination (works with Linker options below)
        add_compile_options(-ffunction-sections -fdata-sections)
        
        # Linker Optimizations
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # --gc-sections: Remove unused code sections
            # -s: Strip symbols (smaller binary)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -s")
        endif()
    endif()
    
    set(LINK_DL dl)
endif()

# link definitions
link_directories(
    ${LIB_DIR}
    ${LIB_DIR}/gcc830
)

# bin/lib
# Tip: Comment out unused executables to speed up compilation
add_executable(pkicli ${MAIN_DIR}/pki.cc)
add_executable(shardora ${MAIN_DIR}/main.cc)
add_executable(https ${MAIN_DIR}/http_svr.cc)
add_executable(httpc ${MAIN_DIR}/http_cli.cc)
add_executable(wss ${MAIN_DIR}/ws_svr.cc)
add_executable(wsc ${MAIN_DIR}/ws_cli.cc)
add_executable(tnets ${MAIN_DIR}/tnet_svr.cc)
add_executable(tnetc ${MAIN_DIR}/tnet_cli.cc)
add_executable(txcli ${MAIN_DIR}/tx_cli.cc)
add_executable(blsmain ${MAIN_DIR}/bls_main.cc)
add_executable(hotstuff ${MAIN_DIR}/hotstuff.cc)

set(LINK_ARGS
    init
    consensus
    sync
    zjcvm
    block
    vss
    ck
    elect
    timeblock
    pools
    bls
    network
    broadcast
    dht
    transport
    contract
    pki
    big_num
    db
    security
    common
    protos
    tcmalloc # Memory allocator, critical for multi-threaded performance
    ${LINK_PTHREAD}
    ${LINK_DL}
    ${LINK_RT}
    ${LINK_ANDROID_LOG}
    ${CFLIB}
    ${LINK_YAML}
    pthread
    gmp # Big number library, performance critical
    -static-libstdc++
)

# Unified Linking
foreach(TARGET_NAME pkicli blsmain shardora tnetc tnets txcli hotstuff)
    target_link_libraries(${TARGET_NAME} ${LINK_ARGS})
endforeach()

include_directories(SYSTEM ${SRC_DIR})

# Subdirectories
# Adjusted order to ensure dependencies are clear (though CMake usually handles this)
add_subdirectory(${SRC_DIR}/common common)
add_subdirectory(${SRC_DIR}/transport transport)
add_subdirectory(${SRC_DIR}/dht dht)
add_subdirectory(${SRC_DIR}/broadcast broadcast)
add_subdirectory(${SRC_DIR}/network network)
add_subdirectory(${SRC_DIR}/init init)
add_subdirectory(${SRC_DIR}/security security)
add_subdirectory(${SRC_DIR}/protos protos)
add_subdirectory(${SRC_DIR}/tnet tnet)
add_subdirectory(${SRC_DIR}/bls bls)
add_subdirectory(${SRC_DIR}/db db)
add_subdirectory(${SRC_DIR}/pools pools)
add_subdirectory(${SRC_DIR}/zjcvm zjcvm)
add_subdirectory(${SRC_DIR}/big_num big_num)
add_subdirectory(${SRC_DIR}/contract contract)
add_subdirectory(${SRC_DIR}/elect elect)
add_subdirectory(${SRC_DIR}/consensus consensus)
add_subdirectory(${SRC_DIR}/block block)
add_subdirectory(${SRC_DIR}/timeblock timeblock)
add_subdirectory(${SRC_DIR}/ck ck)
add_subdirectory(${SRC_DIR}/vss vss)
add_subdirectory(${SRC_DIR}/sync sync)
add_subdirectory(${SRC_DIR}/pki pki)

# Tests (Consider wrapping in 'if(BUILD_TESTING)' to speed up Release builds if tests aren't needed)
add_subdirectory(${SRC_DIR}/common/tests common_test)
add_subdirectory(${SRC_DIR}/broadcast/tests broadcast_test)
add_subdirectory(${SRC_DIR}/security/tests security_test)
add_subdirectory(${SRC_DIR}/transport/tests transport_test)
add_subdirectory(${SRC_DIR}/bls/tests bls_test)
add_subdirectory(${SRC_DIR}/db/tests db_test)
add_subdirectory(${SRC_DIR}/pools/tests pools_test)
add_subdirectory(${SRC_DIR}/zjcvm/tests zjcvm_test)
add_subdirectory(${SRC_DIR}/big_num/tests bignum_test)
add_subdirectory(${SRC_DIR}/contract/tests contract_test)
add_subdirectory(${SRC_DIR}/elect/tests elect_test)
add_subdirectory(${SRC_DIR}/consensus/tests consensus_test)
add_subdirectory(${SRC_DIR}/consensus/hotstuff/tests hotstuff_test)
add_subdirectory(${SRC_DIR}/ck/tests ck_test)
add_subdirectory(${SRC_DIR}/vss/tests vss_test)
add_subdirectory(${SRC_DIR}/sync/tests sync_test)
