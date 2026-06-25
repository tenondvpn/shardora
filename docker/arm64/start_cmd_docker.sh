#!/usr/bin/env bash
# Wrapper for start_cmd.sh inside Docker (Mac / unprivileged): skip sysctl writes.
set -euo pipefail
export SHARDORA_SKIP_SYSCTL="${SHARDORA_SKIP_SYSCTL:-1}"
exec bash /root/start_cmd.sh "$@"
