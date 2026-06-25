#!/usr/bin/env bash
# Rebuild libuSockets.a without LTO (fixes "LTO version 15.1 instead of 13.1" at link time).
set -euo pipefail
SHARDORA_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$SHARDORA_ROOT/third_party/uWebSockets"
git submodule update --init --recursive 2>/dev/null || true
cd uSockets
make clean || true
WITH_OPENSSL=1 make -j"$(nproc)" CFLAGS="-O3 -fPIC -fno-lto" CXXFLAGS="-fno-lto" LDFLAGS="-fno-lto"
mkdir -p "$SHARDORA_ROOT/third_party/lib"
cp -f uSockets.a "$SHARDORA_ROOT/third_party/lib/libuSockets.a"
echo "OK: $SHARDORA_ROOT/third_party/lib/libuSockets.a (no LTO)"
echo "Reconfigure shardora build without LTO, e.g.:"
echo "  cd cbuild_Release && cmake .. -DSHARDORA_ENABLE_LTO=OFF && make -j\$(nproc) shardora"
