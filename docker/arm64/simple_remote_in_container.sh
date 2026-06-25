#!/usr/bin/env bash
# Same deploy flow as simple_remote_docker.sh but runs directly in the current OS
# (e.g. Ubuntu inside a Docker container) — no docker build / docker run.
#
# temp_cmd_docker.sh and start_cmd.sh expect binaries and templates under /root/pkg
# and instance dirs under /root/shardoras. This script resolves your bundle directory then
# symlinks it to /root/pkg when needed.
#
# Usage (from repo root, inside the container):
#   bash docker/arm64/simple_remote_in_container.sh <each_nodes_count> <public_ip> [end_shard] [Release|Debug] [first_node_count]
#
# Env (same ideas as simple_remote_docker.sh):
#   USE_PKG_TAR=1, SHARDORA_STAGING, SHARDORA_PKG_DIR, SHARDORA_NO_ROOT_PKG_FALLBACK
#   SHARDORA_SKIP_SYSCTL=1 (default applied in start_cmd_in_container.sh)

set -euo pipefail

EACH="${1:?each_nodes_count}"
PUBLIC_IP="${2:?public_ip}"
END_SHARD="${3:-3}"
TARGET="${4:-Release}"
FIRST_NODE="${5:-$EACH}"

SHARDORA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGING="${SHARDORA_STAGING:-${SHARDORA_ROOT}/docker/arm64/staging}"

mkdir -p "${STAGING}"

if [[ "${USE_PKG_TAR:-0}" == "1" && -f "${STAGING}/pkg.tar.gz" ]]; then
  rm -rf "${STAGING}/pkg"
  mkdir -p "${STAGING}"
  tar -xzf "${STAGING}/pkg.tar.gz" -C "${STAGING}"
fi

resolve_pkg_dir() {
  local d
  if [[ -n "${SHARDORA_PKG_DIR:-}" ]]; then
    d="$(cd "${SHARDORA_PKG_DIR}" && pwd)"
    if [[ -f "${d}/shardora" ]]; then
      echo "${d}"
      return 0
    fi
    echo "SHARDORA_PKG_DIR=${SHARDORA_PKG_DIR} does not contain ./shardora" >&2
    exit 1
  fi
  if [[ -d "${STAGING}/pkg" && -f "${STAGING}/pkg/shardora" ]]; then
    echo "$(cd "${STAGING}/pkg" && pwd)"
    return 0
  fi
  if [[ "${SHARDORA_NO_ROOT_PKG_FALLBACK:-0}" != "1" && -d /root/pkg && -f /root/pkg/shardora ]]; then
    echo ">>> Using /root/pkg as deploy bundle (${STAGING}/pkg missing)." >&2
    echo "/root/pkg"
    return 0
  fi
  return 1
}

PKG_DIR=""
if ! PKG_DIR="$(resolve_pkg_dir)"; then
  echo "Missing deployment bundle (need a directory with ./shardora, same layout as remote /root/pkg)."
  echo "  Tried: SHARDORA_PKG_DIR, then ${STAGING}/pkg, then /root/pkg (unless SHARDORA_NO_ROOT_PKG_FALLBACK=1)."
  echo "Fix one of:"
  echo "  - unpack pkg into ${STAGING}/pkg/"
  echo "  - USE_PKG_TAR=1 with ${STAGING}/pkg.tar.gz"
  echo "  - export SHARDORA_PKG_DIR=/path/to/pkg"
  echo "  - Or use /root/pkg (remote-style layout)."
  exit 1
fi

same_dir() {
  local a b
  a="$(cd "$1" && pwd)"
  b="$(cd "$2" && pwd)"
  [[ "$a" == "$b" ]]
}

ensure_root_pkg() {
  local src="$1"
  src="$(cd "$src" && pwd)"

  if [[ -f /root/pkg/shardora ]]; then
    if same_dir /root/pkg "$src"; then
      return 0
    fi
    echo "Refusing: /root/pkg exists but is not the same directory as bundle ${src}" >&2
    echo "  Remove or replace /root/pkg, or set SHARDORA_PKG_DIR to that tree." >&2
    exit 1
  fi

  if [[ -e /root/pkg ]]; then
    if [[ -L /root/pkg ]]; then
      mkdir -p /root
      ln -sfn "$src" /root/pkg
      echo ">>> Symlink /root/pkg -> ${src}" >&2
      return 0
    fi
    echo "Refusing: /root/pkg exists but has no ./shardora and is not a symlink." >&2
    exit 1
  fi

  mkdir -p /root
  ln -sfn "$src" /root/pkg
  echo ">>> Symlink /root/pkg -> ${src}" >&2
}

ensure_root_pkg "${PKG_DIR}"

if date -u -v+240d +%s >/dev/null 2>&1; then
  leader_init_tm=$(date -u -v+240d +%s)
else
  leader_init_tm=$(date -u -d "+240 days" +%s)
fi

TEMP="${SHARDORA_ROOT}/docker/arm64/temp_cmd_docker.sh"
STARTC="${SHARDORA_ROOT}/docker/arm64/start_cmd_in_container.sh"
chmod +x "$TEMP" "$STARTC" 2>/dev/null || true

echo "Running temp_cmd_docker + start (in-container, no nested Docker)..."
echo "  public_ip=${PUBLIC_IP} start=1 first_count=${FIRST_NODE} each=${EACH} end_shard=${END_SHARD}"

bash "$TEMP" "${PUBLIC_IP}" 1 "${FIRST_NODE}" 0 2 "${END_SHARD}" "${leader_init_tm}"
export SHARDORA_SKIP_SYSCTL="${SHARDORA_SKIP_SYSCTL:-1}"
bash "$STARTC" "${PUBLIC_IP}" 1 "${FIRST_NODE}" 0 2 "${END_SHARD}"

echo "Done. Node data under /root/shardoras (TARGET=${TARGET} — must match how shardora/txcli in pkg were built)."
