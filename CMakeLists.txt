# option
option(BUILD_SHARED_LIBS "build shared library(.so/.a)" OFF)
option(BUILD_CLIENT "build shardora client" OFF)
option(SHARDORA_MOCK_VERIFY "Enable mocked signature verification" OFF)
option(SHARDORA_MOCK_SIGN "Enable mocked signing" OFF)

# version required
cmake_minimum_required(VERSION 3.22.0)
project(shardora)

if(POLICY CMP0069)
  cmake_policy(SET CMP0069 NEW)
endif()

# Whitebox Curve25519 keys for init (build.sh also passes -DREPLACE_WHITEBOX_*).
if(NOT DEFINED REPLACE_WHITEBOX_PK OR REPLACE_WHITEBOX_PK STREQUAL "")
  find_program(SHARDORA_OPENSSL openssl)
  find_program(SHARDORA_XXD xxd)
  if(NOT SHARDORA_OPENSSL OR NOT SHARDORA_XXD)
    message(FATAL_ERROR
      "REPLACE_WHITEBOX_PK not set. Use: bash build.sh shardora Release\n"
      "  or install openssl + xxd and re-run cmake from an empty build dir.")
  endif()
  set(_shardora_wb_keys "${CMAKE_BINARY_DIR}/shardora_whitebox_keys.cmake")
  execute_process(
    COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/gen_whitebox_keys.cmake.sh" "${_shardora_wb_keys}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE _shardora_wb_rc
  )
  if(_shardora_wb_rc OR NOT EXISTS "${_shardora_wb_keys}")
    message(FATAL_ERROR "Failed to generate whitebox keys (openssl genpkey / xxd).")
  endif()
  include("${_shardora_wb_keys}")
  message(STATUS "Generated ephemeral REPLACE_WHITEBOX_PK/SK at configure time")
endif()

# =========================================================
# 1. Default Build Type to Release (Performance First)
# =========================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected, defaulting to Release for performance.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# =========================================================
# 2. Optional LTO (off by default: third_party/*.a are often built with another GCC)
# =========================================================
option(SHARDORA_ENABLE_LTO "Enable IPO/LTO for Release (all static deps must be built with the same compiler/LTO)" OFF)
if(SHARDORA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        message(STATUS "IPO/LTO enabled for Release (SHARDORA_ENABLE_LTO=ON).")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "SHARDORA_ENABLE_LTO=ON but IPO/LTO is not supported: ${ipo_output}")
    endif()
else()
    message(STATUS "IPO/LTO disabled (default). Use -DSHARDORA_ENABLE_LTO=ON only if third_party libs were built with matching LTO.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF CACHE BOOL "IPO for Release" FORCE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "IPO all configs" FORCE)
    # Clear -flto left in cache from older configures (otherwise link still invokes lto-wrapper).
    foreach(_var CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE CMAKE_EXE_LINKER_FLAGS_RELEASE CMAKE_SHARED_LINKER_FLAGS_RELEASE)
        if(DEFINED ${_var})
            string(REGEX REPLACE "(^| )-flto(=[^ ]*)?($| )" " " ${_var} "${${_var}}")
            string(STRIP "${${_var}}" ${_var})
            set(${_var} "${${_var}}" CACHE STRING "" FORCE)
        endif()
    endforeach()
    add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fno-lto>)
    add_link_options($<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fno-lto>)
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
set(UWEBSOCKETS_LIB "${DEP_DIR}/lib/libuSockets.a")
if(NOT EXISTS "${UWEBSOCKETS_LIB}" AND EXISTS "${DEP_DIR}/lib64/libuSockets.a")
    set(UWEBSOCKETS_LIB "${DEP_DIR}/lib64/libuSockets.a")
endif()

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
    add_compile_definitions(SHARDORA_USE_UV)
    set(LINK_RT )
    set(LINK_DL )
    if(NOT DEFINED IOS_PLATFORM)
        set(LIB_DIR
            ${DEP_DIR}/libs/mac
            ${DEP_DIR}/lib
            ${DEP_DIR}/lib64)
    else() # iOS
        message(STATUS "IOS_PLATFORM=${IOS_PLATFORM}")
        set(LIB_DIR ${DEP_DIR}/libs/ios)
    endif()
    if(DEFINED HOMEBREW_PREFIX AND NOT HOMEBREW_PREFIX STREQUAL "")
        list(APPEND LIB_DIR ${HOMEBREW_PREFIX}/lib)
    endif()
    find_package(OpenSSL REQUIRED)
    include_directories(${OPENSSL_INCLUDE_DIR})
    find_library(CFLIB CoreFoundation)
    find_library(SECURITYLIB Security)
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
    ${DEP_DIR}/include/uWebSockets
    ${DEP_DIR}/include/uSockets
    ${DEP_DIR}/libbls/deps/deps_inst/x86_or_x64/include/
    ${CURL_INCLUDE_DIRS}
)

set(REQUIRED_THIRD_PARTY_HEADERS
    ${DEP_DIR}/include/pbc/pbc.h
    ${DEP_DIR}/include/cpppbc/PBC.h
    ${DEP_DIR}/include/GeoLite2PP.hpp
    ${DEP_DIR}/include/libbls/tools/utils.h
    ${DEP_DIR}/include/nlohmann/json.hpp
    ${DEP_DIR}/include/evmc/evmc.hpp)
foreach(required_header IN LISTS REQUIRED_THIRD_PARTY_HEADERS)
    if(NOT EXISTS "${required_header}")
        message(FATAL_ERROR
            "Required third-party header is missing: ${required_header}. "
            "Run bash build_third.sh and reconfigure from a clean build directory.")
    endif()
endforeach()

set(REQUIRED_THIRD_PARTY_LIBRARIES
    ${UWEBSOCKETS_LIB})
foreach(required_library IN LISTS REQUIRED_THIRD_PARTY_LIBRARIES)
    if(NOT EXISTS "${required_library}")
        message(FATAL_ERROR
            "Required third-party library is missing: ${required_library}. "
            "Run bash build_third.sh and reconfigure from a clean build directory.")
    endif()
endforeach()

# compile definitions
add_definitions(
    -DTEST_LOCAL_NETWORK
    -DOPENSSL_ROOT_DIR=/usr/local/Cellar/openssl/1.0.2r
    -DENABLE_HOTSTUFF
    -DDISABLE_GENESIS_BLS_VERIFY
    -DSHARDORA_USE_UV
    #    -DSHARDORA_UNITTEST # for testing
    #-DFOR_CONSOLE_DEBUG
#    -DTEST_TRANSPORT_PERFOMANCE
#    -DSHARDORA_TRACE_MESSAGE
#  -DLEVELDB
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

if(SHARDORA_MOCK_VERIFY)
    add_definitions(-DMOCK_VERIFY)
endif()

if(SHARDORA_MOCK_SIGN)
    add_definitions(-DMOCK_SIGN)
endif()

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

    # AddressSanitizer: detect heap corruption, use-after-free, buffer overflow
    # Build with: cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=ON ..
    option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
    if(ENABLE_ASAN)
        message(STATUS "AddressSanitizer ENABLED")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -O1 -g)
        add_link_options(-fsanitize=address)
    endif()

    # Extreme Performance Optimization for Release Build
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ENABLE_ASAN)
        # -O3: Highest optimization level
        # -march=native: Use all instruction sets available on the host CPU (AVX2/AVX512 etc).
        #                Huge performance boost for BLS/Crypto.
        # -funroll-loops: Aggressive loop unrolling
        add_compile_options(-O3 -march=native -funroll-loops)

        # Enable Dead Code Elimination (works with Linker options below)
        add_compile_options(-ffunction-sections -fdata-sections)

        # Linker Optimizations
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # --gc-sections: Remove unused code sections
            # -s: Strip symbols (smaller binary)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -s")
        endif()
    endif()

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(LINK_DL dl)
    endif()
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

set(LINK_ARGS
    init
    consensus
    sync
    shardoravm
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
    ${LINK_PTHREAD}
    ${LINK_DL}
    ${LINK_RT}
    ${LINK_ANDROID_LOG}
    ${CFLIB}
    ${LINK_YAML}
    pthread
    gmp # Big number library, performance critical
    ${UWEBSOCKETS_LIB} # uWebSockets dependency
    z # zlib for uWebSockets
    ssl # OpenSSL for HTTPS
    crypto # OpenSSL crypto
    #-static-libstdc++
    #-static
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND LINK_ARGS ${SECURITYLIB} ${DEP_DIR}/pbc/libpbc.a)
endif()

# tcmalloc conflicts with AddressSanitizer, only link when ASAN is off
if(NOT ENABLE_ASAN)
    list(APPEND LINK_ARGS tcmalloc)
    # gperftools tcmalloc may be built with libunwind for stack traces (Linux containers).
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_library(SHARDORA_UNWIND_LIBRARY unwind)
        if(SHARDORA_UNWIND_LIBRARY)
            list(APPEND LINK_ARGS ${SHARDORA_UNWIND_LIBRARY})
        endif()
    endif()
endif()

set(SHARDORA_LINK_LIBRARIES)
set(SHARDORA_LINK_OPTIONS)
foreach(LINK_ARG IN LISTS LINK_ARGS)
    if(LINK_ARG MATCHES "^-")
        list(APPEND SHARDORA_LINK_OPTIONS "${LINK_ARG}")
    else()
        list(APPEND SHARDORA_LINK_LIBRARIES "${LINK_ARG}")
    endif()
endforeach()

# Unified Linking
set(SHARDORA_EXECUTABLE_TARGETS pkicli blsmain shardora tnetc tnets txcli)
foreach(TARGET_NAME IN LISTS SHARDORA_EXECUTABLE_TARGETS)
    target_link_libraries(${TARGET_NAME} ${SHARDORA_LINK_LIBRARIES})
    if(SHARDORA_LINK_OPTIONS)
        target_link_options(${TARGET_NAME} PRIVATE ${SHARDORA_LINK_OPTIONS})
    endif()
    if(NOT SHARDORA_ENABLE_LTO)
        set_target_properties(${TARGET_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
        target_compile_options(${TARGET_NAME} PRIVATE $<$<CONFIG:Release>:-fno-lto>)
        target_link_options(${TARGET_NAME} PRIVATE $<$<CONFIG:Release>:-fno-lto>)
    endif()
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
add_subdirectory(${SRC_DIR}/websocket websocket)
add_subdirectory(${SRC_DIR}/security security)
add_subdirectory(${SRC_DIR}/protos protos)
add_subdirectory(${SRC_DIR}/tnet tnet)
add_subdirectory(${SRC_DIR}/bls bls)
add_subdirectory(${SRC_DIR}/db db)
add_subdirectory(${SRC_DIR}/pools pools)
add_subdirectory(${SRC_DIR}/shardoravm shardoravm)
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

# Tests (disable with -DBUILD_TESTING=OFF for faster or minimal tree builds)
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
add_subdirectory(${SRC_DIR}/common/tests common_test)
add_subdirectory(${SRC_DIR}/network/tests network_test)
add_subdirectory(${SRC_DIR}/broadcast/tests broadcast_test)
add_subdirectory(${SRC_DIR}/security/tests security_test)
add_subdirectory(${SRC_DIR}/transport/tests transport_test)
add_subdirectory(${SRC_DIR}/bls/tests bls_test)
add_subdirectory(${SRC_DIR}/db/tests db_test)
add_subdirectory(${SRC_DIR}/dht/tests dht_test)
add_subdirectory(${SRC_DIR}/pools/tests pools_test)
add_subdirectory(${SRC_DIR}/shardoravm/tests shardoravm_test)
add_subdirectory(${SRC_DIR}/big_num/tests bignum_test)
add_subdirectory(${SRC_DIR}/contract/tests contract_test)
add_subdirectory(${SRC_DIR}/elect/tests elect_test)
add_subdirectory(${SRC_DIR}/consensus/tests consensus_test)
add_subdirectory(${SRC_DIR}/consensus/hotstuff/tests hotstuff_test)
add_subdirectory(${SRC_DIR}/vss/tests vss_test)
add_subdirectory(${SRC_DIR}/sync/tests sync_test)
add_subdirectory(${SRC_DIR}/protos/tests protos_test)
add_subdirectory(${SRC_DIR}/block/tests block_test)
add_subdirectory(${SRC_DIR}/timeblock/tests tmblock_test)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_subdirectory(${SRC_DIR}/tnet/tests tnet_test)
endif()

add_subdirectory(${SRC_DIR}/init/tests init_test)
add_subdirectory(${SRC_DIR}/websocket/tests websocket_test)
if(EXISTS "${SRC_DIR}/main/tests/CMakeLists.txt")
    add_subdirectory(${SRC_DIR}/main/tests main_test)
else()
    message(STATUS "Skipping main tests: ${SRC_DIR}/main/tests/CMakeLists.txt not found")
endif()
add_subdirectory(${SRC_DIR}/pki/tests pki_test)
endif()
