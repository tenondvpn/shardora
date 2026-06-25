#!/usr/bin/env bash
set -euo pipefail

# Build third-party dependencies for Shardora on macOS.
#
# The project CMake searches both third_party/lib and third_party/libs/mac on
# Darwin.  This script installs into third_party/ first, then mirrors static
# libraries into third_party/libs/mac so older CMake/link invocations keep
# working.
#
# Usage:
#   ./build_third_mac.sh
#   SKIP_BREW=1 ./build_third_mac.sh
#   CLEAN_THIRD=1 ./build_third_mac.sh

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build_third_mac.sh is intended for macOS only." >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD="$ROOT/third_party"
PREFIX="$THIRD"
LIB_MAC="$THIRD/libs/mac"
JOBS="${JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
export CFLAGS="${CFLAGS:-} -fPIC -fno-lto"
export CXXFLAGS="${CXXFLAGS:-} -fPIC -fno-lto"
export LDFLAGS="${LDFLAGS:-} -fno-lto"

log() {
    printf '\n==> %s\n' "$*"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

have() {
    command -v "$1" >/dev/null 2>&1
}

brew_prefix_for() {
    local formula="$1"
    if have brew; then
        brew --prefix "$formula" 2>/dev/null || true
    fi
}

brew_install() {
    [[ "${SKIP_BREW:-0}" == "1" ]] && return 0
    if ! have brew; then
        echo "Homebrew not found. Install it or rerun with SKIP_BREW=1 after preparing tools manually." >&2
        return 1
    fi

    local pkgs=(
        autoconf automake libtool pkg-config cmake ninja yasm
        boost gmp openssl@3 zstd lz4 snappy bzip2 xz
    )
    log "Ensuring Homebrew build packages"
    brew install "${pkgs[@]}" || true
}

openssl_prefix() {
    local prefix="${OPENSSL_ROOT_DIR:-}"
    if [[ -z "$prefix" ]]; then
        prefix="$(brew_prefix_for openssl@3)"
    fi
    if [[ -z "$prefix" ]]; then
        prefix="$(brew_prefix_for openssl@1.1)"
    fi
    printf '%s' "$prefix"
}

cmake_build_install() {
    local name="$1"
    local src="$2"
    shift 2
    local build_dir="$src/build_mac"

    [[ -d "$src" ]] || die "Missing dependency source: $src"
    log "Building $name"
    rm -rf "$build_dir"
    cmake -S "$src" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        "$@"
    cmake --build "$build_dir" -j "$JOBS"
    cmake --install "$build_dir"
}

run_if_missing() {
    local marker="$1"
    local name="$2"
    shift 2
    if [[ -e "$marker" ]]; then
        log "Skipping $name (already installed: $marker)"
        return 0
    fi
    "$@"
}

sync_static_libs() {
    mkdir -p "$LIB_MAC"
    find "$THIRD/lib" "$THIRD/lib64" -maxdepth 1 -type f \( -name '*.a' -o -name '*.dylib' \) \
        -exec cp -f {} "$LIB_MAC"/ \; 2>/dev/null || true
}

patch_text() {
    local file="$1"
    local from="$2"
    local to="$3"
    [[ -f "$file" ]] || return 0
    perl -0pi -e "s{$from}{$to}g" "$file"
}

prepare_submodules() {
    log "Initializing git submodules"
    git submodule sync --recursive
    (cd "$THIRD/evmone" && git checkout -- lib/evmone/CMakeLists.txt 2>/dev/null || true)
    (cd "$THIRD/geolite2pp" && git checkout -- src-lib/CMakeLists.txt src-main/main.cpp 2>/dev/null || true)
    (cd "$THIRD/maxmind" && git checkout -- src/maxminddb.c 2>/dev/null || true)
    (cd "$THIRD/libbls" && git checkout -- deps/build.sh build.sh 2>/dev/null || true)
    local paths=(
        third_party/log4cpp
        third_party/maxmind
        third_party/geolite2pp
        third_party/protobuf
        third_party/xxHash
        third_party/ethash
        third_party/gmssl
        third_party/gperftools
        third_party/readerwriterqueue
        third_party/secp256k1
        third_party/boost/multiprecision
        third_party/oqs
        third_party/libsodium
        third_party/libbls
        third_party/leveldb
        third_party/httplib
        third_party/evmone
        third_party/clickhouse
        third_party/cpppbc
        third_party/json
        third_party/pbc
        third_party/fmt
        third_party/zstd
        third_party/gtest
        third_party/rocksdb
        third_party/spdlog
        third_party/libuv
        third_party/uWebSockets
    )
    git submodule update --init "${paths[@]}"
}

build_protobuf() {
    local src="$THIRD/protobuf"
    (cd "$src" && git checkout 48cb18e || true)
    if [[ -x "$src/autogen.sh" ]]; then
        log "Building protobuf"
        (cd "$src" && ./autogen.sh && ./configure \
            --disable-shared --enable-static \
            CXXFLAGS="-fPIC -O2 -fno-lto" CFLAGS="-fPIC -O2 -fno-lto" \
            --prefix="$PREFIX" && make -j "$JOBS" && make install)
    else
        cmake_build_install protobuf "$src" \
            -Dprotobuf_BUILD_TESTS=OFF \
            -Dprotobuf_BUILD_SHARED_LIBS=OFF
    fi
}

build_libuv() {
    local src="$THIRD/libuv"
    (cd "$src" && git checkout 5152db2 || true)
    cmake_build_install libuv "$src" -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF
    if [[ -d "$PREFIX/include/uv" && ! -d "$PREFIX/include/libuv" ]]; then
        mv "$PREFIX/include/uv" "$PREFIX/include/libuv"
    fi
    if [[ -f "$PREFIX/include/uv.h" ]]; then
        patch_text "$PREFIX/include/uv.h" '"uv/' '"libuv/'
    fi
    [[ -f "$PREFIX/include/libuv/unix.h" ]] && patch_text "$PREFIX/include/libuv/unix.h" '"uv/' '"libuv/'
    [[ -f "$PREFIX/include/libuv/win.h" ]] && patch_text "$PREFIX/include/libuv/win.h" '"uv/' '"libuv/'
}

build_gmssl() {
    local src="$THIRD/gmssl"
    (cd "$src" && git checkout d655c06 || true)
    if [[ -f "$src/include/gmssl/sm2_recover.h" ]] && ! grep -q 'gmssl/sm2.h' "$src/include/gmssl/sm2_recover.h"; then
        perl -0pi -e 's{#include <gmssl/sm3.h>\n}{#include <gmssl/sm3.h>\n#include <gmssl/sm2.h>\n}' "$src/include/gmssl/sm2_recover.h"
    fi
    cmake_build_install gmssl "$src" -DENABLE_SM2_EXTS=ON
}

build_leveldb() {
    local src="$THIRD/leveldb"
    (cd "$src" && git checkout 99b3c03 || true)
    cmake_build_install leveldb "$src" \
        -DLEVELDB_BUILD_TESTS=OFF \
        -DLEVELDB_BUILD_BENCHMARKS=OFF
}

build_rocksdb() {
    local src="$THIRD/rocksdb"
    patch_text "$src/CMakeLists.txt" '-march=native' ''
    cmake_build_install rocksdb "$src" \
        -DWITH_TESTS=OFF \
        -DWITH_GFLAGS=OFF \
        -DWITH_TOOLS=OFF \
        -DWITH_BENCHMARK_TOOLS=OFF \
        -DPORTABLE=ON
}

build_secp256k1() {
    local src="$THIRD/secp256k1"
    (cd "$src" && git checkout a660a49 || true)
    log "Building secp256k1"
    (cd "$src" && ./autogen.sh && ./configure \
        --enable-module-ecdh \
        --with-internal-keccak \
        --disable-ecmult-static-precomputation \
        --enable-module-recovery \
        --enable-module-schnorrsig \
        --disable-shared \
        --enable-static \
        --prefix="$PREFIX" && make -j "$JOBS" && make install)
}

build_pbc() {
    local src="$THIRD/pbc"
    local gmp_prefix="${GMP_ROOT:-$(brew_prefix_for gmp)}"
    local pbc_cppflags="-Iinclude -I. -fPIC"
    local pbc_ldflags=""
    if [[ -n "$gmp_prefix" ]]; then
        pbc_cppflags="$pbc_cppflags -I$gmp_prefix/include"
        pbc_ldflags="-L$gmp_prefix/lib"
    fi
    log "Building pbc"
    if [[ -x "$src/configure" || -x "$src/autogen.sh" ]]; then
        (cd "$src" && [[ -x ./autogen.sh ]] && ./autogen.sh || true && ./configure \
            --disable-shared --enable-static --prefix="$PREFIX" \
            CPPFLAGS="$pbc_cppflags" LDFLAGS="$pbc_ldflags" && make -j "$JOBS" && make install)
    else
        (cd "$src" && make -f simple.make libpbc.a CPPFLAGS="$pbc_cppflags" LDFLAGS="$pbc_ldflags")
        mkdir -p "$PREFIX/include/pbc" "$PREFIX/lib"
        cp -R "$src/include/"* "$PREFIX/include/pbc/"
        cp -f "$src"/lib*.a "$PREFIX/lib/" 2>/dev/null || true
    fi

    if [[ -f "$PREFIX/include/pbc/pbcxx.h" ]]; then
        patch_text "$PREFIX/include/pbc/pbcxx.h" 'private:' 'public:'
        patch_text "$PREFIX/include/pbc/pbcxx.h" 'protected:' 'public:'
    fi
}

build_cpppbc() {
    local src="$THIRD/cpppbc"
    local gmp_prefix="${GMP_ROOT:-$(brew_prefix_for gmp)}"
    local cpppbc_flags="-I../include -L../lib -g -O2 -Wall -fPIC"
    if [[ -n "$gmp_prefix" ]]; then
        cpppbc_flags="$cpppbc_flags -I$gmp_prefix/include -L$gmp_prefix/lib"
    fi
    [[ -d "$src" ]] || return 0
    log "Building cpppbc"
    (cd "$src" && git checkout . || true)
    (cd "$src" && make -j "$JOBS" libPBC.a CXXFLAGS="$cpppbc_flags")
    mkdir -p "$PREFIX/include/cpppbc" "$PREFIX/lib"
    cp -f "$src"/*.h "$PREFIX/include/cpppbc/" 2>/dev/null || true
    cp -f "$src"/lib*.a "$PREFIX/lib/" 2>/dev/null || true
}

patch_libbls_deps_build_script() {
    local script="$1"
    [[ -f "$script" ]] || return 0
    local tmp
    tmp="$(mktemp)"
    awk '
        !done_darwin && /if \[ "\$UNIX_SYSTEM_NAME" = "Darwin" \];/ {
            print "if [ \"$UNIX_SYSTEM_NAME\" = \"Darwin\" ];"
            print "then"
            print "\t#export NUMBER_OF_CPU_CORES=$(system_profiler | awk '\''/Number Of CPUs/{print $4}{next;}'\'')"
            print "\texport NUMBER_OF_CPU_CORES=$(sysctl -n hw.ncpu)"
            print "\tif [ -x /opt/homebrew/bin/greadlink ]; then"
            print "\t\texport READLINK=/opt/homebrew/bin/greadlink"
            print "\telif [ -x /usr/local/bin/greadlink ]; then"
            print "\t\texport READLINK=/usr/local/bin/greadlink"
            print "\telse"
            print "\t\texport READLINK=readlink"
            print "\tfi"
            print "\texport SO_EXT=dylib"
            print "fi"
            in_darwin = 1
            done_darwin = 1
            next
        }
        in_darwin {
            if ($0 ~ /^# detect working/) {
                print ""
                print
                in_darwin = 0
            }
            next
        }
        { print }
    ' "$script" > "$tmp"
    mv "$tmp" "$script"
    tmp="$(mktemp)"
    awk '
        /sed -i '\''3594i\\#ifdef MIE_USE_X64ASM'\''/ { next }
        /sed -i '\''s\/bool\/int\/g'\''[[:space:]]+deps\/argtable2\/src\/arg_int\.c/ {
            print "\t\tperl -0pi -e '\''s/bool/int/g'\'' deps/argtable2/src/arg_int.c"
            next
        }
        /eval git checkout 03b719a7c81757071f99fc60be1f7f7694e51390/ {
            print
            print "\t\techo -e \"${COLOR_INFO}patching libff zm2.cpp for non-x64 asm${COLOR_DOTS}...${COLOR_RESET}\""
            print "\t\tperl -0pi -e '\''s@(\\t\\t// setup code and data area\\n\\t\\tconst int PageSize = 4096;\\n\\t\\tconst size_t codeSize = PageSize \\* 9;\\n\\t\\tconst size_t dataSize = PageSize \\* 1;\\n\\n\\t\\tstatic std::vector<Xbyak::uint8> buf;\\n\\t\\tbuf.resize\\(codeSize \\+ dataSize \\+ PageSize\\);\\n\\t\\tXbyak::uint8 \\*const codeAddr = Xbyak::CodeArray::getAlignedAddress\\(&buf\\[0\\], PageSize\\);\\n\\t\\tXbyak::CodeArray::protect\\(codeAddr, codeSize, true\\);\\n\\t\\ts_data = Xbyak::CastTo<Data\\*>\\(codeAddr \\+ codeSize\\);\\n\\n//\\t\\tprintf\\(\"codeAddr=%p, dataAddr=%p\\\\n\", codeAddr, s_data\\);\\n\\t\\tif \\(\\(size_t\\)codeAddr & 0xffffffff00000000ULL \\|\\| \\(size_t\\)s_data & 0xffffffff00000000ULL\\) \\{\\n\\t\\t\\t// printf\\(\"\\\\naddress of code and data is over 4GB!!!\\\\n\"\\);\\n\\t\\t\\}\\n)@#ifdef MIE_USE_X64ASM\\n$1#else\\n\\t\\tstatic Data data;\\n\\t\\ts_data = \\&data;\\n#endif\\n@s'\'' ./depends/ate-pairing/src/zm2.cpp"
            print "\t\tperl -0pi -e '\''s@(\\t\\t// setup code\\n\\t\\tstatic PairingCode code\\(codeSize, codeAddr\\);\\n\\t\\tcode.init\\(p_, mode, useMulx\\);\\n)@#ifdef MIE_USE_X64ASM\\n$1#endif\\n@s'\'' ./depends/ate-pairing/src/zm2.cpp"
            next
        }
        { print }
    ' "$script" > "$tmp"
    mv "$tmp" "$script"
    chmod +x "$script"
    bash -n "$script"
}

build_libbls() {
    local src="$THIRD/libbls"
    local openssl_root
    openssl_root="$(openssl_prefix)"
    log "Building libbls"
    (cd "$src" && git checkout -- deps/build.sh build.sh 2>/dev/null || true)
    if [[ -f "$ROOT/scripts/patch_argtable2_arg_int.sh" && -f "$src/deps/argtable2/src/arg_int.c" ]]; then
        bash "$ROOT/scripts/patch_argtable2_arg_int.sh" "$src/deps/argtable2/src/arg_int.c"
    fi
    patch_libbls_deps_build_script "$src/deps/build.sh"
    rm -rf "$src/deps/libff/build"
    (cd "$src/deps" && PARALLEL_COUNT=1 bash build.sh)
    [[ -f "$src/deps/deps_inst/x86_or_x64/lib/libff.a" ]] || die "libbls dependency libff did not build"
    mkdir -p "$src/deps/deps_inst/x86_or_x64/lib"
    cp -f "$src"/deps/deps_inst/x86_or_x64/lib64/lib* "$src"/deps/deps_inst/x86_or_x64/lib/ 2>/dev/null || true
    mkdir -p "$PREFIX/include/boost"
    cp -R "$src"/deps/deps_inst/x86_or_x64/include/boost/* "$PREFIX/include/boost/" 2>/dev/null || true
    cmake_build_install libbls "$src" \
        -DDEPS_INSTALL_ROOT="$src/deps/deps_inst/x86_or_x64" \
        -DCMAKE_INCLUDE_PATH="$src/deps/deps_inst/x86_or_x64/include;$openssl_root/include" \
        -DCMAKE_LIBRARY_PATH="$src/deps/deps_inst/x86_or_x64/lib;$openssl_root/lib" \
        -DCMAKE_PREFIX_PATH="$src/deps/deps_inst/x86_or_x64;$openssl_root" \
        -DCMAKE_C_FLAGS="-I$openssl_root/include" \
        -DCMAKE_CXX_FLAGS="-I$openssl_root/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$openssl_root/lib" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$openssl_root/lib" \
        -DCRYPTOPP_LIBRARY="$openssl_root/lib/libcrypto.a" \
        -DUSE_ASM=OFF \
        -DWITH_PROCPS=OFF \
        -DLIBBLS_BUILD_TESTS=OFF
    mkdir -p "$PREFIX/include/libbls" "$PREFIX/lib"
    cp -R "$src/third_party" "$src/tools" "$src/dkg" "$src/bls" "$PREFIX/include/libbls/"
    if [[ -f "$src/build_mac/libbls.a" ]]; then
        cp -f "$src/build_mac/libbls.a" "$PREFIX/lib/libdkgbls.a"
    elif [[ -f "$PREFIX/lib/libbls.a" ]]; then
        cp -f "$PREFIX/lib/libbls.a" "$PREFIX/lib/libdkgbls.a"
    fi
    cp -f "$src"/deps/deps_inst/x86_or_x64/lib/libff.a "$PREFIX/lib/" 2>/dev/null || true
    cp -f "$src"/deps/deps_inst/x86_or_x64/lib/libgmp*.a "$PREFIX/lib/" 2>/dev/null || true
}

build_evmone_and_evmc() {
    local src="$THIRD/evmone"
    (cd "$src" && git fetch --tags || true && git checkout v0.11.0 || true && git submodule update --init --recursive)
    if [[ "$(uname -s)" == "Darwin" ]]; then
        patch_text "$src/lib/evmone/CMakeLists.txt" 'add_standalone_library\(evmone\)' '# add_standalone_library(evmone) disabled on macOS'
        rm -rf "$src/build_mac"
    fi
    cmake_build_install evmone "$src" \
        -DEVMC_INSTALL=ON \
        -DEVMONE_TESTING=OFF \
        -DEVMONE_TOOLS=OFF
    if [[ -d "$src/evmc/include/evmc" && ! -d "$PREFIX/include/evmc" ]]; then
        mkdir -p "$PREFIX/include"
        cp -R "$src/evmc/include/evmc" "$PREFIX/include/"
    fi
    if [[ -d "$src/evmc" && ! -d "$PREFIX/include/evmc" ]]; then
        cmake_build_install evmc "$src/evmc" -DETHERSCORE=OFF -DEVMC_TESTING=OFF
    fi
}

build_maxmind_and_geolite2pp() {
    local maxmind="$THIRD/maxmind"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        patch_text "$maxmind/src/maxminddb.c" 'bswap32' 'mmdb_bswap32'
        patch_text "$maxmind/src/maxminddb.c" 'bswap64' 'mmdb_bswap64'
        rm -rf "$maxmind/build_mac"
    fi
    cmake_build_install maxmind "$maxmind" -DBUILD_TESTING=OFF
    mkdir -p "$PREFIX/include/maxmind"
    cp -R "$maxmind/include/"* "$PREFIX/include/maxmind/" 2>/dev/null || true
    mkdir -p "$PREFIX/include/maxmind/include"
    cp -R "$maxmind/include/"* "$PREFIX/include/maxmind/include/" 2>/dev/null || true
    [[ -f "$maxmind/build_mac/generated/maxminddb_config.h" ]] && cp -f "$maxmind/build_mac/generated/maxminddb_config.h" "$PREFIX/include/maxmind/"
    [[ -f "$maxmind/build_mac/generated/maxminddb_config.h" ]] && cp -f "$maxmind/build_mac/generated/maxminddb_config.h" "$PREFIX/include/maxmind/include/"

    local geo="$THIRD/geolite2pp"
    patch_text "$geo/src-main/main.cpp" 'const auto iter' 'const auto\& iter'
    if [[ "$(uname -s)" == "Darwin" ]]; then
        patch_text "$geo/src-lib/CMakeLists.txt" 'ADD_LIBRARY[[:space:]]*\([[:space:]]*geolite2\+\+s SHARED \$\{LIB_SOURCE\}[[:space:]]*\)\nSET_TARGET_PROPERTIES[[:space:]]*\([[:space:]]*geolite2\+\+s PROPERTIES OUTPUT_NAME "geolite2\+\+"[[:space:]]*\)\nSET_TARGET_PROPERTIES[[:space:]]*\([[:space:]]*geolite2\+\+s PROPERTIES PREFIX "lib"[[:space:]]*\)\n\nINSTALL[[:space:]]*\([[:space:]]*TARGETS geolite2\+\+ geolite2\+\+s ARCHIVE DESTINATION lib LIBRARY DESTINATION lib[[:space:]]*\)' 'INSTALL ( TARGETS geolite2++ ARCHIVE DESTINATION lib )'
    fi
    rm -rf "$geo/build_mac"
    cmake_build_install geolite2pp "$geo" \
        -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DCMAKE_INCLUDE_PATH="$PREFIX/include" \
        -DCMAKE_C_FLAGS="-I$PREFIX/include" \
        -DCMAKE_CXX_FLAGS="-I$PREFIX/include"
}

build_uwebsockets() {
    local src="$THIRD/uWebSockets"
    local openssl_root
    openssl_root="$(openssl_prefix)"
    [[ -d "$src" ]] || die "Missing dependency source: $src"
    log "Building uWebSockets/uSockets"
    (cd "$src" && git checkout v20.64.0 || true && git submodule update --init uSockets)
    mkdir -p "$PREFIX/include/uWebSockets" "$PREFIX/include" "$PREFIX/lib"
    cp -f "$src/src/"*.h "$PREFIX/include/uWebSockets/"
    cp -f "$src/uSockets/src/"*.h "$PREFIX/include/"
    (cd "$src/uSockets" && make clean || true && WITH_OPENSSL=1 make -j "$JOBS" \
        override CFLAGS="-O2 -fPIC -fno-lto -I$openssl_root/include" \
        override CXXFLAGS="-O2 -fPIC -fno-lto -I$openssl_root/include" \
        override LDFLAGS="-fno-lto -L$openssl_root/lib")
    if [[ -f "$src/uSockets/src/crypto/openssl.c" && ! -f "$src/uSockets/openssl.o" ]]; then
        (cd "$src/uSockets" && cc -O2 -fPIC -fno-lto -I"$openssl_root/include" \
            -DLIBUS_USE_OPENSSL -std=c11 -Isrc -c src/crypto/openssl.c -o openssl.o)
    fi
    if [[ ! -f "$src/uSockets/uSockets.a" ]]; then
        (cd "$src/uSockets" && ar rcs uSockets.a ./*.o)
    fi
    (cd "$src/uSockets" && ar rcs uSockets.a ./*.o)
    cp -f "$src/uSockets/uSockets.a" "$PREFIX/lib/libuSockets.a"
}

install_header_only() {
    run_if_missing "$PREFIX/include/httplib.h" httplib \
        bash -c "mkdir -p '$PREFIX/include' && cp -f '$THIRD/httplib/httplib.h' '$PREFIX/include/'"
    run_if_missing "$PREFIX/include/readerwriterqueue" readerwriterqueue \
        bash -c "mkdir -p '$PREFIX/include/readerwriterqueue' && cp -f '$THIRD/readerwriterqueue/'*.h '$PREFIX/include/readerwriterqueue/'"
}

build_simple_deps() {
    run_if_missing "$PREFIX/include/gtest" gtest cmake_build_install gtest "$THIRD/gtest" -DBUILD_GMOCK=ON -DINSTALL_GTEST=ON
    run_if_missing "$PREFIX/include/fmt" fmt cmake_build_install fmt "$THIRD/fmt" -DFMT_TEST=OFF
    run_if_missing "$PREFIX/include/spdlog" spdlog cmake_build_install spdlog "$THIRD/spdlog" -DSPDLOG_BUILD_TESTS=OFF -DSPDLOG_FMT_EXTERNAL=OFF
    if [[ -d "$THIRD/spdlog/include/spdlog/fmt/bundled" && ! -f "$PREFIX/include/spdlog/fmt/bundled/printf.h" ]]; then
        mkdir -p "$PREFIX/include/spdlog/fmt"
        cp -R "$THIRD/spdlog/include/spdlog/fmt/bundled" "$PREFIX/include/spdlog/fmt/"
    fi
    run_if_missing "$PREFIX/include/nlohmann" json cmake_build_install json "$THIRD/json" -DJSON_BuildTests=OFF
    run_if_missing "$PREFIX/include/zstd" zstd cmake_build_install zstd "$THIRD/zstd/build/cmake" -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF
    run_if_missing "$PREFIX/include/ethash" ethash cmake_build_install ethash "$THIRD/ethash" -DETHASH_BUILD_TESTS=OFF
    run_if_missing "$PREFIX/include/oqs" oqs cmake_build_install oqs "$THIRD/oqs" -DOQS_BUILD_ONLY_LIB=ON -DOQS_BUILD_TESTS=OFF
    run_if_missing "$PREFIX/include/sodium" libsodium bash -c "cd '$THIRD/libsodium' && ./autogen.sh -s && ./configure --disable-shared --enable-static --prefix='$PREFIX' && make -j '$JOBS' && make install"
    run_if_missing "$PREFIX/include/gperftools" gperftools bash -c "cd '$THIRD/gperftools' && ./autogen.sh && ./configure --disable-shared --enable-static --prefix='$PREFIX' && make -j '$JOBS' && make install"
    run_if_missing "$PREFIX/include/boost/multiprecision" boost-multiprecision cmake_build_install boost-multiprecision "$THIRD/boost/multiprecision"
    run_if_missing "$PREFIX/include/clickhouse" clickhouse cmake_build_install clickhouse "$THIRD/clickhouse" -DCLICKHOUSE_CPP_BUILD_TESTS=OFF
    if [[ -d "$THIRD/clickhouse/contrib/absl/absl" ]]; then
        cp -R "$THIRD/clickhouse/contrib/absl/absl" "$PREFIX/include/" 2>/dev/null || true
    fi
    if [[ -d "$THIRD/clickhouse/contrib/lz4/lz4" ]]; then
        cp -R "$THIRD/clickhouse/contrib/lz4/lz4" "$PREFIX/include/" 2>/dev/null || true
    fi
    if [[ -d "$THIRD/clickhouse/contrib/zstd/zstd" ]]; then
        cp -R "$THIRD/clickhouse/contrib/zstd/zstd" "$PREFIX/include/" 2>/dev/null || true
    fi
    run_if_missing "$PREFIX/include/xxHash" xxHash bash -c "cd '$THIRD/xxHash' && make -j '$JOBS' && mkdir -p '$PREFIX/include/xxHash' '$PREFIX/lib' && cp -f ./*.h '$PREFIX/include/xxHash/' && cp -f cachedObjs/*/libxxhash.a '$PREFIX/lib/'"
}

if [[ "${CLEAN_THIRD:-0}" == "1" ]]; then
    log "Removing installed third_party include/lib outputs"
    rm -rf "$PREFIX/include" "$PREFIX/lib" "$PREFIX/lib64" "$LIB_MAC"
fi

mkdir -p "$PREFIX/include" "$PREFIX/lib" "$PREFIX/lib64" "$LIB_MAC"

brew_install
prepare_submodules

run_if_missing "$PREFIX/include/google/protobuf" protobuf build_protobuf
run_if_missing "$PREFIX/include/libuv" libuv build_libuv
run_if_missing "$PREFIX/include/gmssl" gmssl build_gmssl
run_if_missing "$PREFIX/include/leveldb" leveldb build_leveldb
run_if_missing "$PREFIX/include/rocksdb" rocksdb build_rocksdb
run_if_missing "$PREFIX/include/secp256k1.h" secp256k1 build_secp256k1
run_if_missing "$PREFIX/include/pbc" pbc build_pbc
run_if_missing "$PREFIX/include/cpppbc" cpppbc build_cpppbc
run_if_missing "$PREFIX/include/libbls" libbls build_libbls
run_if_missing "$PREFIX/include/evmc" evmone-evmc build_evmone_and_evmc
run_if_missing "$PREFIX/include/GeoLite2PP.hpp" geolite2pp build_maxmind_and_geolite2pp
run_if_missing "$PREFIX/include/libusockets.h" uWebSockets build_uwebsockets

build_simple_deps
install_header_only
sync_static_libs

log "Removing shared libraries to keep Shardora static-link friendly"
find "$PREFIX/lib" "$PREFIX/lib64" "$LIB_MAC" -type f \( -name '*.so' -o -name '*.so.*' \) -delete 2>/dev/null || true

log "macOS third-party dependencies are ready"
echo "Includes: $PREFIX/include"
echo "Libraries: $PREFIX/lib and $LIB_MAC"
