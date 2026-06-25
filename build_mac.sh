#!/usr/bin/env bash
set -euo pipefail

# Build Shardora on macOS.
#
# Usage:
#   ./build_mac.sh                  # build shardora, Release
#   ./build_mac.sh shardora Debug       # build shardora in Debug
#   ./build_mac.sh txcli Release    # build another CMake target
#   ./build_mac.sh test Debug       # build and run tests through CTest
#   SKIP_THIRD=1 ./build_mac.sh     # skip third-party dependency build step

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build_mac.sh is intended for macOS only." >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CMD="${1:-shardora}"
BUILD_TYPE="${2:-Release}"
EXTRA_MODE="${3:-}"
JOBS="${JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"

if [[ "$BUILD_TYPE" != "Debug" && "$BUILD_TYPE" != "Release" ]]; then
    echo "Unknown build type '$BUILD_TYPE'. Use Debug or Release." >&2
    exit 1
fi

have() {
    command -v "$1" >/dev/null 2>&1
}

brew_prefix_for() {
    local formula="$1"
    if have brew; then
        brew --prefix "$formula" 2>/dev/null || true
    fi
}

BREW_PREFIX=""
if have brew; then
    BREW_PREFIX="$(brew --prefix 2>/dev/null || true)"
fi

OPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-}"
if [[ -z "$OPENSSL_ROOT_DIR" ]]; then
    OPENSSL_ROOT_DIR="$(brew_prefix_for openssl@3)"
fi
if [[ -z "$OPENSSL_ROOT_DIR" ]]; then
    OPENSSL_ROOT_DIR="$(brew_prefix_for openssl@1.1)"
fi
if [[ -z "$OPENSSL_ROOT_DIR" ]]; then
    echo "OpenSSL was not found through Homebrew. Install openssl@3 or set OPENSSL_ROOT_DIR." >&2
    exit 1
fi

XXD_BIN="${XXD_BIN:-$(command -v xxd || true)}"
OPENSSL_BIN="${OPENSSL_BIN:-$OPENSSL_ROOT_DIR/bin/openssl}"
if [[ ! -x "$OPENSSL_BIN" ]]; then
    OPENSSL_BIN="$(command -v openssl || true)"
fi
if [[ -z "$OPENSSL_BIN" || -z "$XXD_BIN" ]]; then
    echo "openssl and xxd are required to generate whitebox keys." >&2
    exit 1
fi

if [[ "${SKIP_THIRD:-0}" != "1" ]]; then
    "$ROOT/build_third_mac.sh"
fi

BUILD_DIR="$ROOT/cbuild_${BUILD_TYPE}_mac"
mkdir -p "$BUILD_DIR"

KEY_DIR="$BUILD_DIR/whitebox"
mkdir -p "$KEY_DIR"
"$OPENSSL_BIN" genpkey -algorithm x25519 -out "$KEY_DIR/private_key.pem" 2>/dev/null

RAW_SK=$("$OPENSSL_BIN" pkey -in "$KEY_DIR/private_key.pem" -outform DER | tail -c 32 | "$XXD_BIN" -p -c 32)
RAW_PK=$("$OPENSSL_BIN" pkey -in "$KEY_DIR/private_key.pem" -pubout -outform DER | tail -c 32 | "$XXD_BIN" -p -c 32)

format_to_c_array() {
    echo "$(echo "$1" | sed 's/../0x&,/g' | sed 's/,$//')"
}

PK_ARRAY="$(format_to_c_array "$RAW_PK")"
SK_ARRAY="$(format_to_c_array "$RAW_SK")"

rm -f "$ROOT"/third_party/lib/lib*.so* "$ROOT"/third_party/lib64/lib*.so* 2>/dev/null || true

COMMON_CMAKE_ARGS=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_INSTALL_PREFIX="$ROOT/install_mac"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR"
    -DCMAKE_PREFIX_PATH="$ROOT/third_party;$OPENSSL_ROOT_DIR;$BREW_PREFIX"
    -DHOMEBREW_PREFIX="$BREW_PREFIX"
    -DREPLACE_WHITEBOX_PK="$PK_ARRAY"
    -DREPLACE_WHITEBOX_SK="$SK_ARRAY"
    -DENABLE_ASAN=OFF
    -DSHARDORA_ENABLE_LTO=OFF
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF
)

if [[ "$CMD" == "coverage" || "$EXTRA_MODE" == "coverage" ]]; then
    COMMON_CMAKE_ARGS+=(
        -DXENABLE_CODE_COVERAGE=ON
        -DCMAKE_C_FLAGS=--coverage\ -O0
        -DCMAKE_CXX_FLAGS=--coverage\ -O0
        -DCMAKE_EXE_LINKER_FLAGS=--coverage
        -DCMAKE_SHARED_LINKER_FLAGS=--coverage
    )
fi

cmake "${COMMON_CMAKE_ARGS[@]}"

if [[ "$CMD" == "test" ]]; then
    cmake --build "$BUILD_DIR" -j "$JOBS"
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j "$JOBS"
elif [[ "$CMD" == "coverage" ]]; then
    cmake --build "$BUILD_DIR" -j "$JOBS"
else
    cmake --build "$BUILD_DIR" --target "$CMD" -j "$JOBS"
fi

if [[ -x "$BUILD_DIR/$CMD" ]]; then
    echo "Built: $BUILD_DIR/$CMD"
else
    echo "Build completed in: $BUILD_DIR"
fi
