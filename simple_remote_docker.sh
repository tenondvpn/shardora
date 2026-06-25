#!/usr/bin/env bash
# Same directory as simple_remote.sh — Docker (linux/arm64) single-host flow on a machine **with Docker**.
# For running inside an Ubuntu container without nested Docker, use simple_remote_in_container.sh instead.
#
# Mirrors simple_remote.sh argument positions where it makes sense:
#   $1 each_nodes_count
#   $2 node_ips (comma-separated → first IP/host used as PUBLIC_IP for the container)
#   $3 end_shard (default 3)
#   $4 PASSWORD — ignored in Docker (no SSH)
#   $5 TARGET (default Release). If you omit $4 and pass only 4 args total, $4 can be Debug|Release as TARGET.
#   $6 first_node_count (default same as $1)
#
# Examples (from repo root):
#   bash simple_remote_docker.sh 1 host.docker.internal
#   bash simple_remote_docker.sh 1 192.168.1.10,192.168.1.11 3
#   bash simple_remote_docker.sh 1 10.0.0.1 3 '' Release
#
# SHARDORA_PKG_DIR, SHARDORA_STAGING, USE_PKG_TAR, SHARDORA_NO_ROOT_PKG_FALLBACK, etc. pass through to docker/arm64/simple_remote_docker.sh (see docker/arm64/README.md).
# For no nested Docker, use simple_remote_in_container.sh (same env keys apply to that script).
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
IPS="${2:-host.docker.internal}"
PUB="${IPS%%,*}"

if [ -n "${5:-}" ]; then
  TARGET="$5"
elif [ "${4:-}" = "Debug" ] || [ "${4:-}" = "Release" ]; then
  TARGET="$4"
else
  TARGET=Release
fi
FIRST="${6:-$1}"

exec bash "$ROOT/docker/arm64/simple_remote_docker.sh" \
  "${1:?each_nodes_count}" \
  "$PUB" \
  "${3:-3}" \
  "$TARGET" \
  "$FIRST"
