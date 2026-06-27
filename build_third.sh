#!/bin/bash
set -e

export nproc=${nproc:-8}
export TARGET=${TARGET:-Release}
SRC_PATH=`pwd`
COMPLETE_MARKER="$SRC_PATH/third_party/.build_third_complete"
USOCKETS_BUILD_MARKER="$SRC_PATH/third_party/lib/libuSockets.no_lto.openssl.stamp"
SUDO=""
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
fi

installed_file_exists() {
    local file="$1"
    if [ -e "$file" ]; then
        return 0
    fi

    case "$file" in
        "$SRC_PATH"/third_party/lib/*.a)
            local archive_name
            archive_name="$(basename "$file")"
            [ -e "$SRC_PATH/third_party/lib64/$archive_name" ]
            return
            ;;
    esac

    return 1
}

required_installed_files=(
    "$SRC_PATH/third_party/include/evmone/evmone.h"
    "$SRC_PATH/third_party/include/evmc/evmc.hpp"
    "$SRC_PATH/third_party/include/sodium.h"
    "$SRC_PATH/third_party/include/maxmind/maxminddb.h"
    "$SRC_PATH/third_party/include/uv.h"
    "$SRC_PATH/third_party/include/pbc/pbc.h"
    "$SRC_PATH/third_party/include/libbls/tools/utils.h"
    "$SRC_PATH/third_party/include/libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp"
    "$SRC_PATH/third_party/include/google/protobuf/message.h"
    "$SRC_PATH/third_party/include/spdlog/spdlog.h"
    "$SRC_PATH/third_party/include/rocksdb/db.h"
    "$SRC_PATH/third_party/include/gmssl/sm2.h"
    "$SRC_PATH/third_party/include/gperftools/tcmalloc.h"
    "$SRC_PATH/third_party/include/secp256k1.h"
    "$SRC_PATH/third_party/include/gtest/gtest.h"
    "$SRC_PATH/third_party/include/zstd.h"
    "$SRC_PATH/third_party/include/fmt/format.h"
    "$SRC_PATH/third_party/include/cpppbc/PBC.h"
    "$SRC_PATH/third_party/include/nlohmann/json.hpp"
    "$SRC_PATH/third_party/include/clickhouse/client.h"
    "$SRC_PATH/third_party/include/GeoLite2PP.hpp"
    "$SRC_PATH/third_party/include/xxHash/xxhash.h"
    "$SRC_PATH/third_party/include/ethash/ethash.hpp"
    "$SRC_PATH/third_party/include/readerwriterqueue/readerwriterqueue.h"
    "$SRC_PATH/third_party/include/boost/multiprecision/cpp_int.hpp"
    "$SRC_PATH/third_party/include/oqs/oqs.h"
    "$SRC_PATH/third_party/include/leveldb/db.h"
    "$SRC_PATH/third_party/include/httplib.h"
    "$SRC_PATH/third_party/include/libusockets.h"
    "$SRC_PATH/third_party/include/uWebSockets/App.h"
    "$SRC_PATH/third_party/lib/libevmone.a"
    "$SRC_PATH/third_party/lib/libevmc-loader.a"
    "$SRC_PATH/third_party/lib/libevmc-instructions.a"
    "$SRC_PATH/third_party/lib/libsodium.a"
    "$SRC_PATH/third_party/lib/libmaxminddb.a"
    "$SRC_PATH/third_party/lib/libuv.a"
    "$SRC_PATH/third_party/lib/libdkgbls.a"
    "$SRC_PATH/third_party/lib/libff.a"
    "$SRC_PATH/third_party/lib/libprotobuf.a"
    "$SRC_PATH/third_party/lib/libspdlog.a"
    "$SRC_PATH/third_party/lib/librocksdb.a"
    "$SRC_PATH/third_party/lib/libgmssl.a"
    "$SRC_PATH/third_party/lib/libtcmalloc.a"
    "$SRC_PATH/third_party/lib/libsecp256k1.a"
    "$SRC_PATH/third_party/lib/libgtest.a"
    "$SRC_PATH/third_party/lib/libgtest_main.a"
    "$SRC_PATH/third_party/lib/libgmock.a"
    "$SRC_PATH/third_party/lib/libgmock_main.a"
    "$SRC_PATH/third_party/lib/libzstd.a"
    "$SRC_PATH/third_party/lib/libfmt.a"
    "$SRC_PATH/third_party/lib/libboost_system.a"
    "$SRC_PATH/third_party/lib/libboost_thread.a"
    "$SRC_PATH/third_party/lib/libboost_atomic.a"
    "$SRC_PATH/third_party/lib/libpbc.a"
    "$SRC_PATH/third_party/lib/libPBC.a"
    "$SRC_PATH/third_party/lib/libclickhouse-cpp-lib.a"
    "$SRC_PATH/third_party/lib/libcityhash.a"
    "$SRC_PATH/third_party/lib/liblz4.a"
    "$SRC_PATH/third_party/lib/libgeolite2++.a"
    "$SRC_PATH/third_party/lib/libxxhash.a"
    "$SRC_PATH/third_party/lib/libethash.a"
    "$SRC_PATH/third_party/lib/libkeccak.a"
    "$SRC_PATH/third_party/lib/libssl.a"
    "$SRC_PATH/third_party/lib/libcrypto.a"
    "$SRC_PATH/third_party/lib/liboqs.a"
    "$SRC_PATH/third_party/lib/libleveldb.a"
    "$SRC_PATH/third_party/lib/libuSockets.a"
    "$USOCKETS_BUILD_MARKER"
)

all_required_installed=true
for required_file in "${required_installed_files[@]}"; do
    if ! installed_file_exists "$required_file"; then
        all_required_installed=false
        break
    fi
done

if [ "$all_required_installed" = true ]; then
    mkdir -p "$SRC_PATH/third_party"
    printf 'complete\n' > "$COMPLETE_MARKER"
    echo "Third-party install outputs are complete; skipping source submodule refresh."
    exit 0
fi

rm -f "$COMPLETE_MARKER"

reset_invalid_submodule() {
    local path="$1"
    local marker="$2"
    case "$path" in
        third_party/*|clipy/*) ;;
        *)
            echo "Refusing to clean unexpected submodule path: $path"
            return 1
            ;;
    esac

    if [ -d "$path" ] && [ ! -e "$path/$marker" ]; then
        echo "Cleaning incomplete submodule checkout: $path (missing $marker)"
        rm -rf "$path"
    fi
}

reset_invalid_cmake_submodule() {
    local path="$1"
    local cmake_file="$path/CMakeLists.txt"
    case "$path" in
        third_party/*|clipy/*) ;;
        *)
            echo "Refusing to clean unexpected submodule path: $path"
            return 1
            ;;
    esac

    if [ -f "$cmake_file" ] && ! cmake_submodule_file_is_valid "$cmake_file"; then
        echo "Cleaning invalid CMake submodule checkout: $path"
        rm -rf "$path"
    fi
}

git submodule sync --recursive

cmake_submodule_file_is_valid() {
    local cmake_file="$1"
    grep -Eiq '^[[:space:]]*(cmake_minimum_required|project)[[:space:]]*\(' "$cmake_file"
}

refresh_submodule() {
    local path="$1"
    local url
    local commit

    if git submodule update --init --force "$path"; then
        return 0
    fi

    url="$(git config --file .gitmodules --get "submodule.${path}.url" || true)"
    commit="$(git ls-files -s "$path" 2>/dev/null | awk '$1 == "160000" {print $2; exit}')"
    if [ -z "$url" ]; then
        echo "No .gitmodules URL found for $path"
        return 1
    fi

    echo "Submodule pathspec update failed for $path; cloning directly from $url"
    rm -rf "$path"
    mkdir -p "$(dirname "$path")"
    git clone "$url" "$path"
    if [ -n "$commit" ]; then
        git -C "$path" checkout "$commit"
    fi
}

ensure_submodule_file() {
    local path="$1"
    local marker="$2"
    if [ ! -e "$path/$marker" ]; then
        echo "Required submodule file is missing: $path/$marker"
        echo "Refreshing submodule checkout: $path"
        rm -rf "$path"
        refresh_submodule "$path"
    fi
    if [ ! -e "$path/$marker" ]; then
        echo "Required submodule file is still missing after refresh: $path/$marker"
        exit 1
    fi
}

ensure_cmake_submodule() {
    local path="$1"
    ensure_submodule_file "$path" CMakeLists.txt
    if ! cmake_submodule_file_is_valid "$path/CMakeLists.txt"; then
        echo "Invalid CMakeLists.txt in submodule: $path"
        echo "Refreshing submodule checkout: $path"
        rm -rf "$path"
        refresh_submodule "$path"
    fi
    if ! cmake_submodule_file_is_valid "$path/CMakeLists.txt"; then
        echo "Invalid CMakeLists.txt remains after refresh: $path/CMakeLists.txt"
        exit 1
    fi
}

reset_incomplete_install_dir() {
    local path="$1"
    local marker="$2"
    if [ -e "$path" ] && [ ! -e "$marker" ]; then
        echo "Cleaning incomplete install output: $path (missing $marker)"
        rm -rf "$path"
    fi
}

reset_incomplete_component() {
    local path="$1"
    shift

    if [ ! -e "$path" ]; then
        return
    fi

    local marker
    for marker in "$@"; do
        if ! installed_file_exists "$marker"; then
            echo "Cleaning incomplete install output: $path (missing $marker)"
            rm -rf "$path"
            return
        fi
    done
}

require_installed_file() {
    local file="$1"
    if ! installed_file_exists "$file"; then
        echo "Required installed file is missing: $file"
        exit 1
    fi
}

reset_incomplete_install_dir "$SRC_PATH/third_party/include/evmc" "$SRC_PATH/third_party/include/evmc/evmc.hpp"
reset_incomplete_install_dir "$SRC_PATH/third_party/include/maxmind" "$SRC_PATH/third_party/include/maxmind/maxminddb.h"
reset_incomplete_component "$SRC_PATH/third_party/include/libbls" "$SRC_PATH/third_party/include/libbls/tools/utils.h" "$SRC_PATH/third_party/lib/libdkgbls.a"
reset_incomplete_component "$SRC_PATH/third_party/include/libff" "$SRC_PATH/third_party/include/libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp" "$SRC_PATH/third_party/lib/libff.a"
reset_incomplete_component "$SRC_PATH/third_party/include/evmone" "$SRC_PATH/third_party/include/evmone/evmone.h" "$SRC_PATH/third_party/lib/libevmone.a"
reset_incomplete_component "$SRC_PATH/third_party/include/evmc" "$SRC_PATH/third_party/include/evmc/evmc.hpp" "$SRC_PATH/third_party/lib/libevmc-loader.a" "$SRC_PATH/third_party/lib/libevmc-instructions.a"
reset_incomplete_component "$SRC_PATH/third_party/include/sodium" "$SRC_PATH/third_party/include/sodium.h" "$SRC_PATH/third_party/lib/libsodium.a"
reset_incomplete_component "$SRC_PATH/third_party/include/maxmind" "$SRC_PATH/third_party/include/maxmind/maxminddb.h" "$SRC_PATH/third_party/lib/libmaxminddb.a"
reset_incomplete_component "$SRC_PATH/third_party/include/libuv" "$SRC_PATH/third_party/include/uv.h" "$SRC_PATH/third_party/lib/libuv.a"
reset_incomplete_component "$SRC_PATH/third_party/include/google/protobuf" "$SRC_PATH/third_party/include/google/protobuf/message.h" "$SRC_PATH/third_party/lib/libprotobuf.a"
reset_incomplete_component "$SRC_PATH/third_party/include/spdlog" "$SRC_PATH/third_party/include/spdlog/spdlog.h" "$SRC_PATH/third_party/lib/libspdlog.a"
reset_incomplete_component "$SRC_PATH/third_party/include/rocksdb" "$SRC_PATH/third_party/include/rocksdb/db.h" "$SRC_PATH/third_party/lib/librocksdb.a"
reset_incomplete_component "$SRC_PATH/third_party/include/gmssl" "$SRC_PATH/third_party/include/gmssl/sm2.h" "$SRC_PATH/third_party/lib/libgmssl.a"
reset_incomplete_component "$SRC_PATH/third_party/include/gperftools" "$SRC_PATH/third_party/include/gperftools/tcmalloc.h" "$SRC_PATH/third_party/lib/libtcmalloc.a"
reset_incomplete_component "$SRC_PATH/third_party/include/gtest" "$SRC_PATH/third_party/include/gtest/gtest.h" "$SRC_PATH/third_party/lib/libgtest.a" "$SRC_PATH/third_party/lib/libgtest_main.a" "$SRC_PATH/third_party/lib/libgmock.a" "$SRC_PATH/third_party/lib/libgmock_main.a"
reset_incomplete_component "$SRC_PATH/third_party/include/zstd.h" "$SRC_PATH/third_party/include/zstd.h" "$SRC_PATH/third_party/lib/libzstd.a"
reset_incomplete_component "$SRC_PATH/third_party/include/fmt" "$SRC_PATH/third_party/include/fmt/format.h" "$SRC_PATH/third_party/lib/libfmt.a"
reset_incomplete_component "$SRC_PATH/third_party/include/boost" "$SRC_PATH/third_party/include/boost/version.hpp" "$SRC_PATH/third_party/lib/libboost_system.a" "$SRC_PATH/third_party/lib/libboost_thread.a" "$SRC_PATH/third_party/lib/libboost_atomic.a"
reset_incomplete_component "$SRC_PATH/third_party/include/pbc" "$SRC_PATH/third_party/include/pbc/pbc.h" "$SRC_PATH/third_party/include/pbc/pbcxx.h" "$SRC_PATH/third_party/lib/libpbc.a"
reset_incomplete_component "$SRC_PATH/third_party/include/cpppbc" "$SRC_PATH/third_party/include/cpppbc/PBC.h" "$SRC_PATH/third_party/lib/libPBC.a"
reset_incomplete_component "$SRC_PATH/third_party/include/clickhouse" "$SRC_PATH/third_party/include/clickhouse/client.h" "$SRC_PATH/third_party/lib/libclickhouse-cpp-lib.a" "$SRC_PATH/third_party/lib/libcityhash.a" "$SRC_PATH/third_party/lib/liblz4.a"
reset_incomplete_component "$SRC_PATH/third_party/include/xxHash" "$SRC_PATH/third_party/include/xxHash/xxhash.h" "$SRC_PATH/third_party/lib/libxxhash.a"
reset_incomplete_component "$SRC_PATH/third_party/include/ethash" "$SRC_PATH/third_party/include/ethash/ethash.hpp" "$SRC_PATH/third_party/lib/libethash.a" "$SRC_PATH/third_party/lib/libkeccak.a"
reset_incomplete_component "$SRC_PATH/third_party/include/openssl" "$SRC_PATH/third_party/include/openssl/ssl.h" "$SRC_PATH/third_party/lib/libssl.a" "$SRC_PATH/third_party/lib/libcrypto.a"
reset_incomplete_component "$SRC_PATH/third_party/include/oqs" "$SRC_PATH/third_party/include/oqs/oqs.h" "$SRC_PATH/third_party/lib/liboqs.a"
reset_incomplete_component "$SRC_PATH/third_party/include/leveldb" "$SRC_PATH/third_party/include/leveldb/db.h" "$SRC_PATH/third_party/lib/libleveldb.a"

ensure_evmc_checkout() {
    cd "$SRC_PATH/third_party/evmone"
    local evmc_commit
    evmc_commit="$(git ls-files -s evmc 2>/dev/null | awk '$1 == "160000" {print $2; exit}')"
    git submodule update --init evmc || true
    if [ -f evmc/CMakeLists.txt ] && grep -q "cmake_minimum_required" evmc/CMakeLists.txt; then
        return 0
    fi

    echo "Invalid evmc submodule checkout; refreshing evmone submodules"
    rm -rf evmc
    git submodule update --init --force evmc || true
    if [ -f evmc/CMakeLists.txt ] && grep -q "cmake_minimum_required" evmc/CMakeLists.txt; then
        return 0
    fi

    local evmc_url
    evmc_url="$(git config --file .gitmodules --get submodule.evmc.url || true)"
    if [ -z "$evmc_url" ]; then
        evmc_url="https://github.com/ethereum/evmc.git"
    fi

    echo "Cloning evmc directly from $evmc_url"
    rm -rf evmc
    git clone "$evmc_url" evmc
    if [ -n "$evmc_commit" ]; then
        git -C evmc checkout "$evmc_commit" || true
    fi
    if [ -f evmc/CMakeLists.txt ] && grep -q "cmake_minimum_required" evmc/CMakeLists.txt; then
        return 0
    fi

    echo "Invalid evmc CMakeLists.txt remains after direct clone"
    exit 1
}

install_deps() {
    if command -v apt-get >/dev/null 2>&1; then
        $SUDO apt-get update
        $SUDO apt-get install -y autoconf automake libtool build-essential cmake git perl \
            texinfo unzip libgnutls28-dev liblzma-dev pkg-config yasm zlib1g-dev libssh2-1-dev
        $SUDO apt-get install -y libprocps-dev || $SUDO apt-get install -y procps
    elif command -v dnf >/dev/null 2>&1; then
        $SUDO dnf install -y gnutls-devel perl procps-ng-devel texinfo unzip xz-devel autoconf automake libtool cmake git
    elif command -v yum >/dev/null 2>&1; then
        $SUDO yum install -y gnutls-devel perl procps-ng-devel texinfo unzip xz-devel autoconf automake libtool cmake git
    else
        echo "No supported package manager found; assuming build dependencies are already installed."
    fi
}

install_deps

checkout_if_available() {
    local ref="$1"
    if git rev-parse --verify --quiet "${ref}^{commit}" >/dev/null; then
        git checkout "$ref"
    else
        echo "Ref $ref is not available locally; using recorded submodule commit $(git rev-parse --short HEAD)."
    fi
}

build_lib() {
    if [ ! -f "$1" ] && [ ! -d "$1" ]; then
        echo "Building $2..."
        cd $SRC_PATH/$2
        eval "$3"
    else
        echo "Skipping $2 (already built)."
    fi
    cd $SRC_PATH
}


# Build evmone from the recorded submodule checkout.
if [ ! -d "$SRC_PATH/third_party/include/evmone" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/evmone include/evmone/evmone.h
    cd third_party/evmone
    git submodule update --init --recursive

    cmake -S . -B build_release \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_FLAGS="-O2 -g" \
        -DBUILD_SHARED_LIBS=OFF \
        -DEVMC_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/

    cd build_release && make -j${nproc} && make install
fi

# Build evmc from evmone's nested submodule.
if [ ! -d "$SRC_PATH/third_party/include/evmc" ]; then
    cd $SRC_PATH
    ensure_evmc_checkout
    cd "$SRC_PATH/third_party/evmone/evmc"
    cmake -S . -B build_release \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O2" \
        -DETHERSCORE=OFF \
        -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/

    cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/evmc/evmc.hpp"

if [ ! -d "$SRC_PATH/third_party/include/sodium" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/libsodium configure.ac
    cd third_party/libsodium && checkout_if_available 9511c98 && git submodule update --init && ./configure --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/sodium.h"
require_installed_file "$SRC_PATH/third_party/lib/libsodium.a"

if [ ! -d "$SRC_PATH/third_party/include/maxmind" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/maxmind
    cd third_party/maxmind/ && git submodule init && git submodule update && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc}
    cp -rnf libmaxminddb.a $SRC_PATH/third_party/lib/
    mkdir -p $SRC_PATH/third_party/include/maxmind && cd .. && cp -rnf ./include/* $SRC_PATH/third_party/include/maxmind && cp -rnf build_release/generated/maxminddb_config.h $SRC_PATH/third_party/include/maxmind/
    mkdir -p $SRC_PATH/third_party/include/maxmind/include && cp -rnf ./include/* $SRC_PATH/third_party/include/maxmind/include && cp -rnf build_release/generated/maxminddb_config.h $SRC_PATH/third_party/include/maxmind/include
fi
require_installed_file "$SRC_PATH/third_party/include/maxmind/maxminddb.h"
require_installed_file "$SRC_PATH/third_party/lib/libmaxminddb.a"

if [ ! -d "$SRC_PATH/third_party/include/libuv" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/libuv
    cd third_party/libuv && checkout_if_available 5152db2 && git submodule init && git submodule update && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=$TARGET -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install
    rm -rf $SRC_PATH/third_party/include/libuv
    mv $SRC_PATH/third_party/include/uv $SRC_PATH/third_party/include/libuv
    sed -i 's/"uv\//"libuv\//g' $SRC_PATH/third_party/include/uv.h
    sed -i 's/"uv\//"libuv\//g' $SRC_PATH/third_party/include/libuv/unix.h
    sed -i 's/"uv\//"libuv\//g' $SRC_PATH/third_party/include/libuv/win.h
fi
require_installed_file "$SRC_PATH/third_party/include/uv.h"
require_installed_file "$SRC_PATH/third_party/lib/libuv.a"

patch_libbls_argtable2() {
    local arg_int="$SRC_PATH/third_party/libbls/deps/argtable2/src/arg_int.c"
    bash "$SRC_PATH/scripts/patch_argtable2_arg_int.sh" "$arg_int"
}

restore_cached_libbls_deps() {
    local cached="$SRC_PATH/third_party/depends/libbls_deps_inst"
    local target="$SRC_PATH/third_party/libbls/deps/deps_inst"
    if [ -d "$cached/x86_or_x64" ] && [ ! -d "$target/x86_or_x64" ]; then
        echo "Restoring cached libbls dependency outputs."
        mkdir -p "$target"
        cp -rnf "$cached"/* "$target"/
    fi
}

save_cached_libbls_deps() {
    local target="$SRC_PATH/third_party/libbls/deps/deps_inst"
    local cached="$SRC_PATH/third_party/depends/libbls_deps_inst"
    if [ -d "$target/x86_or_x64" ]; then
        mkdir -p "$cached"
        cp -rnf "$target"/* "$cached"/
    fi
}

install_libbls_deps_outputs() {
    local deps_root="$SRC_PATH/third_party/libbls/deps/deps_inst/x86_or_x64"
    require_installed_file "$deps_root/include/boost"
    require_installed_file "$deps_root/lib/libff.a"
    mkdir -p "$SRC_PATH/third_party/include/boost" "$SRC_PATH/third_party/lib"
    for item in "$deps_root/include"/*; do
        # libbls bundles openssl headers only; main openssl build installs libs to third_party/lib.
        if [ "$(basename "$item")" = "openssl" ]; then
            continue
        fi
        cp -rnf "$item" "$SRC_PATH/third_party/include/"
    done
    cp -rnf "$deps_root/include/boost"/* "$SRC_PATH/third_party/include/boost/"
    cp -rnf "$deps_root/lib/libff.a" "$SRC_PATH/third_party/lib/libff.a"
    if compgen -G "$deps_root/lib/libboost_*.a" >/dev/null; then
        cp -rnf "$deps_root"/lib/libboost_*.a "$SRC_PATH/third_party/lib/"
    fi
}

if [ ! -d "$SRC_PATH/third_party/include/libbls" ] || \
        [ ! -f "$SRC_PATH/third_party/include/libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp" ] || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libff.a" || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libdkgbls.a" || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libboost_system.a" || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libboost_thread.a" || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libboost_atomic.a" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/libbls
    git submodule update --init --recursive third_party/libbls
    restore_cached_libbls_deps
    patch_libbls_argtable2
    cd third_party/libbls/deps
    if ! grep -q 'patch_libbls_argtable2_arg_int' build.sh; then
        sed -i '/echo "configuring it..."/i\
if [ -f "argtable2/src/arg_int.c" ] && grep -q "static bool isspace" "argtable2/src/arg_int.c"; then\
    sed -i "s/#include <limits.h>/#include <limits.h>\\n#include <stdbool.h>/" "argtable2/src/arg_int.c"\
    sed -i "s/static bool isspace/static bool argtable_isspace_local/g; s/static char toupper/static char argtable_toupper_local/g; s/isspace(/argtable_isspace_local(/g; s/toupper(/argtable_toupper_local(/g" "argtable2/src/arg_int.c"\
fi\
# patch_libbls_argtable2_arg_int' build.sh
    fi
    PARALLEL_COUNT=1 bash build.sh
    mkdir -p deps_inst/x86_or_x64/lib
    if compgen -G "deps_inst/x86_or_x64/lib64/lib*" >/dev/null; then
        cp -rnf deps_inst/x86_or_x64/lib64/lib* deps_inst/x86_or_x64/lib/
    fi
    save_cached_libbls_deps
    install_libbls_deps_outputs
    cd ..
    cmake -S . -B build_release  -DUSE_ASM=False  -DWITH_PROCPS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DLIBBLS_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/
    cd build_release && make -j8 && make install
    mkdir -p $SRC_PATH/third_party/include/libbls && cp -rnf ../third_party ../tools ../dkg ../bls $SRC_PATH/third_party/include/libbls
    mkdir -p $SRC_PATH/third_party/lib
    cp -rnf ./libbls.a $SRC_PATH/third_party/lib/libdkgbls.a
    install_libbls_deps_outputs
fi
require_installed_file "$SRC_PATH/third_party/include/libbls/tools/utils.h"
require_installed_file "$SRC_PATH/third_party/include/libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp"
require_installed_file "$SRC_PATH/third_party/lib/libff.a"
require_installed_file "$SRC_PATH/third_party/lib/libboost_system.a"
require_installed_file "$SRC_PATH/third_party/lib/libboost_thread.a"
require_installed_file "$SRC_PATH/third_party/lib/libboost_atomic.a"

if [ ! -f "$SRC_PATH/third_party/include/google/protobuf/message.h" ] || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libprotobuf.a"; then
    cd $SRC_PATH
    ensure_submodule_file third_party/protobuf autogen.sh
    cd third_party/protobuf/ && checkout_if_available 48cb18e && ./autogen.sh && ./configure --disable-shared --enable-static CXXFLAGS="-fPIC -O3" CFLAGS="-fPIC -O3" --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/google/protobuf/message.h"
require_installed_file "$SRC_PATH/third_party/lib/libprotobuf.a"

if [ ! -d "$SRC_PATH/third_party/include/spdlog" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/spdlog
    cd third_party/spdlog && git checkout . && git submodule update --init && cmake -S . -B build_release -DSPDLOG_ENABLE_SOURCE_LOC=ON -DWITH_TESTS=OFF -DPORTABLE=1  -DCMAKE_CXX_FLAGS="-Wno-maybe-uninitialized" -DWITH_GFLAGS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/spdlog/spdlog.h"
require_installed_file "$SRC_PATH/third_party/lib/libspdlog.a"

if [ ! -d "$SRC_PATH/third_party/include/rocksdb" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/rocksdb
    cd third_party/rocksdb && git checkout . && sed -i "s/-march=native//g" ./CMakeLists.txt && git submodule update --init && cmake -S . -B build_release -DWITH_TESTS=OFF -DPORTABLE=1  -DCMAKE_CXX_FLAGS="-Wno-maybe-uninitialized" -DWITH_GFLAGS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/rocksdb/db.h"
require_installed_file "$SRC_PATH/third_party/lib/librocksdb.a"

if [ ! -d "$SRC_PATH/third_party/include/gmssl" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/gmssl
    cd third_party/gmssl && checkout_if_available d655c06 && sed -i "s/-march=native//g" ./CMakeLists.txt && sed -i '19i\#include <gmssl/sm2.h>' ./include/gmssl/sm2_recover.h && cmake -S . -B build_release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DENABLE_SM2_EXTS=on -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
    objcopy --localize-symbol=OPENSSL_hexchar2int        --localize-symbol=OPENSSL_hexstr2buf        $SRC_PATH/third_party/lib/libgmssl.a
fi
require_installed_file "$SRC_PATH/third_party/include/gmssl/sm2.h"
require_installed_file "$SRC_PATH/third_party/lib/libgmssl.a"

if [ ! -d "$SRC_PATH/third_party/include/gperftools" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/gperftools autogen.sh
    cd third_party/gperftools/ && checkout_if_available d9a5d38 && ./autogen.sh && ./configure --prefix=$SRC_PATH/third_party/ --disable-libunwind && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/gperftools/tcmalloc.h"
require_installed_file "$SRC_PATH/third_party/lib/libtcmalloc.a"

if [ ! -f "$SRC_PATH/third_party/include/secp256k1.h" ]; then
    cd $SRC_PATH
    #cd third_party/secp256k1 && git checkout a660a49 && cmake -S . -B build_release -DSECP256K1_ENABLE_MODULE_RECOVERY=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
    ensure_submodule_file third_party/secp256k1 autogen.sh
    cd third_party/secp256k1 && checkout_if_available a660a49 && bash ./autogen.sh && ./configure --enable-module-ecdh --with-internal-keccak --disable-ecmult-static-precomputation --enable-module-recovery --enable-module-schnorrsig --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/secp256k1.h"
require_installed_file "$SRC_PATH/third_party/lib/libsecp256k1.a"

if [ ! -d "$SRC_PATH/third_party/include/gtest" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/gtest
    cd third_party/gtest &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/gtest/gtest.h"
require_installed_file "$SRC_PATH/third_party/lib/libgtest.a"
require_installed_file "$SRC_PATH/third_party/lib/libgtest_main.a"
require_installed_file "$SRC_PATH/third_party/lib/libgmock.a"
require_installed_file "$SRC_PATH/third_party/lib/libgmock_main.a"

if [ ! -f "$SRC_PATH/third_party/include/zstd.h" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/zstd
    cd third_party/zstd &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DJSON_BuildTests=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/zstd.h"
require_installed_file "$SRC_PATH/third_party/lib/libzstd.a"

if [ ! -d "$SRC_PATH/third_party/include/fmt" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/fmt
    cd third_party/fmt &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DJSON_BuildTests=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
    # cd $SRC_PATH
    # cd third_party/fmt && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/fmt/format.h"
require_installed_file "$SRC_PATH/third_party/lib/libfmt.a"

if [ ! -d "$SRC_PATH/third_party/include/pbc" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/pbc simple.make
    cd third_party/pbc && make -f simple.make libpbc.a
    mkdir -p $SRC_PATH/third_party/include/pbc
    if [ -d ./include/pbc ]; then
        cp -rnf ./include/pbc/* $SRC_PATH/third_party/include/pbc/
    else
        cp -rnf ./include/* $SRC_PATH/third_party/include/pbc/
    fi
    cp -rnf ./lib*.a  $SRC_PATH/third_party/lib
fi
require_installed_file "$SRC_PATH/third_party/include/pbc/pbc.h"
require_installed_file "$SRC_PATH/third_party/include/pbc/pbcxx.h"
require_installed_file "$SRC_PATH/third_party/lib/libpbc.a"
sed -i 's/private/public/g' "$SRC_PATH/third_party/include/pbc/pbcxx.h"
sed -i 's/protected/public/g' "$SRC_PATH/third_party/include/pbc/pbcxx.h"

if [ ! -f "$SRC_PATH/third_party/include/nlohmann/json.hpp" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/json
    cd third_party/json &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DJSON_BuildTests=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/nlohmann/json.hpp"

if [ ! -d "$SRC_PATH/third_party/include/cpppbc" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/cpppbc Makefile
    cd third_party/cpppbc && git checkout . && sed -i 's/CXXFLAGS=/CXXFLAGS=-I\.\.\/include -L\.\.\/lib /g' ./Makefile && make -j8 libPBC.a
    mkdir -p $SRC_PATH/third_party/include/cpppbc && cp -rnf ./*.h $SRC_PATH/third_party/include/cpppbc
    cp -rnf ./lib*.a $SRC_PATH/third_party/lib
fi
require_installed_file "$SRC_PATH/third_party/include/cpppbc/PBC.h"
require_installed_file "$SRC_PATH/third_party/lib/libPBC.a"

if [ ! -d "$SRC_PATH/third_party/include/clickhouse" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/clickhouse
    cd third_party/clickhouse &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
    sed -i 's/ciso646/version/g' ../contrib/absl/absl/base/options.h
    cp -rnf ../contrib/absl/absl $SRC_PATH/third_party/include/
    cp -rnf ../contrib/lz4/lz4 $SRC_PATH/third_party/include/
    cp -rnf ../contrib/zstd/zstd $SRC_PATH/third_party/include/
fi
require_installed_file "$SRC_PATH/third_party/include/clickhouse/client.h"
require_installed_file "$SRC_PATH/third_party/lib/libclickhouse-cpp-lib.a"
require_installed_file "$SRC_PATH/third_party/lib/libcityhash.a"
require_installed_file "$SRC_PATH/third_party/lib/liblz4.a"


if [ ! -f "$SRC_PATH/third_party/include/GeoLite2PP.hpp" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/geolite2pp
    cd third_party/geolite2pp && git checkout . && sed -i 's/const auto iter/const auto\& iter/g' ./src-main/main.cpp &&  sed -i '11i\include_directories(SYSTEM '$SRC_PATH'/third_party/include/maxmind/)' CMakeLists.txt && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/  -DCMAKE_PREFIX_PATH=$SRC_PATH/third_party/ -DCMAKE_INCLUDE_PATH=$SRC_PATH/third_party/include/maxmind/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/GeoLite2PP.hpp"
require_installed_file "$SRC_PATH/third_party/lib/libgeolite2++.a"


if [ ! -d "$SRC_PATH/third_party/include/xxHash" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/xxHash Makefile
    cd third_party/xxHash/ && make -j${nproc} && mkdir -p $SRC_PATH/third_party/include/xxHash && cp -rnf ./*.h $SRC_PATH/third_party/include/xxHash && cp -rnf cachedObjs/*/libxxhash.a $SRC_PATH/third_party/lib
fi
require_installed_file "$SRC_PATH/third_party/include/xxHash/xxhash.h"
require_installed_file "$SRC_PATH/third_party/lib/libxxhash.a"

if [ ! -d "$SRC_PATH/third_party/include/ethash" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/ethash
    cd third_party/ethash && checkout_if_available 83bd5ad && cmake -S . -B build_release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && mkdir -p $SRC_PATH/third_party/include/ethash && cp -rnf ../include/ethash/* $SRC_PATH/third_party/include/ethash && cp -rnf ./lib/keccak/libkeccak.a ./lib/ethash/libethash.a ./lib/global_context/libethash-global-context.a $SRC_PATH/third_party/lib
fi
require_installed_file "$SRC_PATH/third_party/include/ethash/ethash.hpp"
require_installed_file "$SRC_PATH/third_party/lib/libethash.a"
require_installed_file "$SRC_PATH/third_party/lib/libkeccak.a"


if ! installed_file_exists "$SRC_PATH/third_party/include/openssl/ssl.h" || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libssl.a" || \
        ! installed_file_exists "$SRC_PATH/third_party/lib/libcrypto.a"; then
    rm -rf "$SRC_PATH/third_party/include/openssl"
    cd $SRC_PATH
    ensure_submodule_file third_party/openssl Configure
    cd third_party/openssl/ && checkout_if_available 7b371d8 && ./Configure --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/openssl/ssl.h"
require_installed_file "$SRC_PATH/third_party/lib/libssl.a"
require_installed_file "$SRC_PATH/third_party/lib/libcrypto.a"

if [ ! -d "$SRC_PATH/third_party/include/readerwriterqueue" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/readerwriterqueue readerwriterqueue.h
    cd third_party/readerwriterqueue && checkout_if_available 8b21766 && mkdir -p $SRC_PATH/third_party/include/readerwriterqueue && cp -rnf ./*.h $SRC_PATH/third_party/include/readerwriterqueue
fi
require_installed_file "$SRC_PATH/third_party/include/readerwriterqueue/readerwriterqueue.h"

if [ ! -d "$SRC_PATH/third_party/include/boost/multiprecision" ]; then
    cd $SRC_PATH
    mkdir -p $SRC_PATH/third_party/include/boost
    ensure_cmake_submodule third_party/boost/multiprecision
    cd third_party/boost/multiprecision && checkout_if_available c48ae18 && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
fi
require_installed_file "$SRC_PATH/third_party/include/boost/multiprecision/cpp_int.hpp"

if [ ! -d "$SRC_PATH/third_party/include/oqs" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/oqs
    cd third_party/oqs && checkout_if_available 94b421e && cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install
fi
require_installed_file "$SRC_PATH/third_party/include/oqs/oqs.h"
require_installed_file "$SRC_PATH/third_party/lib/liboqs.a"

if [ ! -d "$SRC_PATH/third_party/include/leveldb" ]; then
    cd $SRC_PATH
    ensure_cmake_submodule third_party/leveldb
    cd third_party/leveldb && checkout_if_available 99b3c03 && git submodule init && git submodule update && cmake -S . -B build_release -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install
fi
require_installed_file "$SRC_PATH/third_party/include/leveldb/db.h"
require_installed_file "$SRC_PATH/third_party/lib/libleveldb.a"

if [ ! -f "$SRC_PATH/third_party/include/httplib.h" ]; then
    cd $SRC_PATH
    ensure_submodule_file third_party/httplib httplib.h
    cd third_party/httplib && cp ./httplib.h $SRC_PATH/third_party/include/
fi
require_installed_file "$SRC_PATH/third_party/include/httplib.h"

# Build uWebSockets and uSockets
if [ ! -f "$SRC_PATH/third_party/include/libusockets.h" ] || \
        [ ! -f "$SRC_PATH/third_party/include/uWebSockets/App.h" ] || \
        [ ! -f "$SRC_PATH/third_party/lib/libuSockets.a" ] || \
        [ ! -f "$USOCKETS_BUILD_MARKER" ]; then
    echo "Building uWebSockets and uSockets..."
    cd $SRC_PATH

    # Use the uWebSockets commit recorded by the parent repository.
    git submodule update --init third_party/uWebSockets
    cd third_party/uWebSockets

    # Use the top-level uSockets submodule. uWebSockets also knows about it
    # as a relative submodule on some tags, but updating it from here can make
    # Git search the parent .gitmodules for "../uSockets".
    cd "$SRC_PATH"
    git submodule update --init third_party/uSockets third_party/uWebSockets
    cd "$SRC_PATH/third_party/uWebSockets"
    if [ ! -f "uSockets/src/libusockets.h" ]; then
        rm -rf uSockets
        ln -s ../uSockets uSockets 2>/dev/null || cp -R ../uSockets uSockets
    fi
    if [ ! -f "uSockets/src/libusockets.h" ]; then
        echo "Required uSockets header is missing: $SRC_PATH/third_party/uSockets/src/libusockets.h"
        echo "Try rerunning: git submodule sync --recursive && git submodule update --init third_party/uSockets third_party/uWebSockets"
        exit 1
    fi

    # Copy uSockets headers to main include directory (NOT in subdirectory!)
    echo "Installing uSockets headers..."
    mkdir -p $SRC_PATH/third_party/include
    cp uSockets/src/*.h $SRC_PATH/third_party/include/

    # Copy uWebSockets headers
    echo "Installing uWebSockets headers..."
    mkdir -p $SRC_PATH/third_party/include/uWebSockets
    cp src/*.h $SRC_PATH/third_party/include/uWebSockets/

    # Build uSockets library (no LTO: must match shardora link when SHARDORA_ENABLE_LTO is off)
    echo "Building uSockets library..."
    cd uSockets
    rm -f ./*.o ./*.a
    "${CC:-cc}" -O3 -fPIC -fno-lto -DLIBUS_USE_OPENSSL -std=c11 -Isrc -c src/*.c src/eventing/*.c src/crypto/*.c src/io_uring/*.c
    "${CXX:-c++}" -O3 -fPIC -fno-lto -DLIBUS_USE_OPENSSL -std=c++17 -Isrc -c src/crypto/*.cpp
    "${AR:-ar}" rvs uSockets.a ./*.o
    mkdir -p $SRC_PATH/third_party/lib
    cp uSockets.a $SRC_PATH/third_party/lib/libuSockets.a
    printf 'no-lto openssl\n' > "$USOCKETS_BUILD_MARKER"

    cd $SRC_PATH
    echo "uWebSockets and uSockets installation completed!"
fi
require_installed_file "$SRC_PATH/third_party/include/libusockets.h"
require_installed_file "$SRC_PATH/third_party/include/uWebSockets/App.h"
require_installed_file "$SRC_PATH/third_party/lib/libuSockets.a"
require_installed_file "$USOCKETS_BUILD_MARKER"

cd $SRC_PATH
rm -rf third_party/lib/lib*.so* third_party/lib64/lib*.so*
for required_file in "${required_installed_files[@]}"; do
    require_installed_file "$required_file"
done
printf 'complete\n' > "$COMPLETE_MARKER"
