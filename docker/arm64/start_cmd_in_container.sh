#!/usr/bin/env bash
# Run start_cmd.sh from the repo checkout (no /root/start_cmd.sh bind mount).
# Use inside an Ubuntu container where temp_cmd_docker.sh already prepared /root/shardoras.
set -euo pipefail
SHARDORA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export SHARDORA_SKIP_SYSCTL="${SHARDORA_SKIP_SYSCTL:-1}"
exec bash "${SHARDORA_ROOT}/start_cmd.sh" "$@"
