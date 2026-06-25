#!/usr/bin/env bash
# Build linux/arm64 shardora + txcli inside Docker (Mac-friendly). Mounts repo at /root/shardora.
# Usage: from repo root — bash docker/arm64/build_linux_arm64.sh [Debug|Release]
set -euo pipefail

TARGET="${1:-Release}"
if [[ "$TARGET" != "Debug" && "$TARGET" != "Release" ]]; then
  echo "Usage: $0 [Debug|Release]"
  exit 1
fi

SHARDORA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$SHARDORA_ROOT"

if ! docker info >/dev/null 2>&1; then
  echo "Docker is not running or not installed."
  exit 1
fi

echo "Building in container: platform=linux/arm64, TARGET=$TARGET"

docker run --rm --platform linux/arm64 \
  -e "TARGET=${TARGET}" \
  -v "${SHARDORA_ROOT}:/root/shardora" \
  -w /root/shardora \
  ubuntu:22.04 \
  bash -lc "set -euo pipefail
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config git curl ca-certificates xxd python3 \
      libssl-dev zlib1g-dev liblz4-dev libzstd-dev \
      libgmp-dev flex bison bzip2 lbzip2
    cd /root/shardora
    bash build.sh shardora \"\${TARGET}\"
    cd \"/root/shardora/cbuild_\${TARGET}\"
    make -j\"\$(nproc)\" txcli
    echo OK: /root/shardora/cbuild_\${TARGET}/shardora
  "

echo "Done. Binaries: ${SHARDORA_ROOT}/cbuild_${TARGET}/shardora , txcli"
