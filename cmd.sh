node_ips=$1
cmd=$2
REMOTE_SSH_PORT="${SHARDORA_REMOTE_SSH_PORT:-${REMOTE_SSH_PORT:-22}}"
REMOTE_SSH_PASSWORD="${SHARDORA_REMOTE_PASSWORD:-${REMOTE_SSH_PASSWORD:-}}"
REMOTE_FAIL_FILE="${REMOTE_FAIL_FILE:-/tmp/shardora_cmd_fail.$$}"
export SSHPASS="Xf4aGbTaf&"
REMOTE_PIDS=()

mark_remote_failure() {
    echo "$1" >> "$REMOTE_FAIL_FILE"
}

is_local_ip() {
    local ip="$1"
    if [ "$ip" = "localhost" ] || [ "$ip" = "127.0.0.1" ]; then
        return 0
    fi
    hostname -I 2>/dev/null | tr ' ' '\n' | grep -Fxq "$ip"
}

run_node_command() {
    local ip="$1"
    if is_local_ip "$ip"; then
        bash -lc "$cmd" 2>&1 | sed "s/^/[$ip] /"
    else
        sshpass -e ssh -o ConnectTimeout=10 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 \
            -p "$REMOTE_SSH_PORT" root@$ip "$cmd 2>&1 | sed \"s/^/[$ip] /\""
    fi
}

check_cmd_finished() {
    echo "waiting..."
    local status=0
    local pid
    for pid in "${REMOTE_PIDS[@]}"; do
        wait "$pid" || status=1
    done
    REMOTE_PIDS=()

    if [ -s "$REMOTE_FAIL_FILE" ]; then
        cat "$REMOTE_FAIL_FILE" >&2
        rm -f "$REMOTE_FAIL_FILE"
        exit 1
    fi
    if [ "$status" -ne 0 ]; then
        exit "$status"
    fi
    echo "waiting ok"
}


clear_command() {
    echo 'run_command start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    for ip in "${node_ips_array[@]}"; do
        (
            run_node_command "$ip" ||
                mark_remote_failure "remote command failed on $ip:$REMOTE_SSH_PORT"
        ) &
        REMOTE_PIDS+=($!)

        run_cmd_count=$(($run_cmd_count + 1))
        if (($run_cmd_count >= 250)); then
            check_cmd_finished
            run_cmd_count=0
        fi
    done

    check_cmd_finished
    echo 'run_command over'
}

clear_command
