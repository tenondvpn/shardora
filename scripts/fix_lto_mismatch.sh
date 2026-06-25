#!/usr/bin/env bash
# Fix: lto1: libuSockets.a LTO version mismatch (e.g. 15.1 vs 13.1).
# Rebuilds libuSockets without LTO and reconfigures cbuild_* with LTO off.
set -euo pipefail
SHARDORA_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-Release}"
BUILD_DIR="${SHARDORA_ROOT}/cbuild_${TARGET}"

echo "=== 1/3 Rebuild libuSockets.a (no LTO) ==="
bash "${SHARDORA_ROOT}/scripts/rebuild_uSockets_nolto.sh"

echo "=== 2/3 Reconfigure CMake (LTO off, clear cached -flto) ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake .. \
  -DCMAKE_BUILD_TYPE="${TARGET}" \
  -DSHARDORA_ENABLE_LTO=OFF \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF \
  -DENABLE_ASAN=OFF

echo "=== 3/3 Clean link of shardora and rebuild ==="
# Drop stale .o built with -flto from a previous configure
find "${BUILD_DIR}" -name '*.o' -path '*/CMakeFiles/shardora.dir/*' -delete 2>/dev/null || true
cmake --build . --target shardora -j"$(nproc 2>/dev/null || echo 4)"

echo "OK: ${BUILD_DIR}/shardora"
