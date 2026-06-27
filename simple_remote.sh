each_nodes_count=${1:-}
node_ips=${2:-}
bootstrap=""
end_shard=${3:-}
PASSWORD=${4:-"Xf4aGbTaf&"}
TARGET=${5:-}
FIRST_NODE_COUNT="$each_nodes_count"
NODE_SSH_PORT="${SHARDORA_REMOTE_NODE_SSH_PORT:-${SHARDORA_REMOTE_SSH_PORT:-22}}"
REMOTE_FAIL_FILE="/tmp/shardora_remote_fail.$$"
#export SHARDORA_REMOTE_SSH_PORT="$NODE_SSH_PORT"
export SHARDORA_REMOTE_PASSWORD="${PASSWORD:-${SHARDORA_REMOTE_PASSWORD:-}}"
export REMOTE_FAIL_FILE
REMOTE_PIDS=()
export SHARDORA_ROOT=/root/shardora
CODE_PATH=`pwd`
node_ips_array=(${node_ips//,/ })
nodes_count=0
for ip in "${node_ips_array[@]}"; do
    nodes_count=$(($nodes_count + $each_nodes_count))
done
node_hash=$(printf "%d%d" "$nodes_count" "$each_nodes_count" | md5sum | cut -d ' ' -f1)

is_container_env() {
    [ -f /.dockerenv ] || [ "${SHARDORA_IN_CONTAINER:-0}" = "1" ]
}

run_privileged() {
    if is_container_env || ! command -v sudo >/dev/null 2>&1; then
        "$@"
    else
        sudo "$@"
    fi
}

ensure_nodes_shardora_conf() {
    local conf_dir="/root/nodes/shardora/conf"
    if [ -e "$conf_dir" ] && [ ! -d "$conf_dir" ]; then
        rm -f "$conf_dir"
    fi
    mkdir -p "$conf_dir" /root/nodes/shardora/log
    if [ -f "$CODE_PATH/nodes_local/shardora/conf/shardora.conf" ]; then
        cp -f "$CODE_PATH/nodes_local/shardora/conf/shardora.conf" "$conf_dir/"
    fi
    if [ -f "$CODE_PATH/nodes_local/shardora/conf/GeoLite2-City.mmdb" ]; then
        cp -f "$CODE_PATH/nodes_local/shardora/conf/GeoLite2-City.mmdb" "$conf_dir/"
    fi
    if [ -f "$CODE_PATH/nodes_local/shardora/conf/log4cpp.properties" ]; then
        cp -f "$CODE_PATH/nodes_local/shardora/conf/log4cpp.properties" "$conf_dir/"
    fi
    if [ ! -f "$conf_dir/shardora.conf" ]; then
        echo "ERROR: missing $conf_dir/shardora.conf" >&2
        exit 1
    fi
}

seed_repo_shard_files() {
    mkdir -p /root/shardora
    if [ -f "$CODE_PATH/shards3" ] && [ ! -f /root/shardora/shards3 ]; then
        cp -f "$CODE_PATH/shards3" /root/shardora/shards3
    fi
    if [ -f "$CODE_PATH/root_nodes" ] && [ ! -f /root/shardora/shards2 ]; then
        cp -f "$CODE_PATH/root_nodes" /root/shardora/shards2
    elif [ ! -f /root/shardora/shards2 ] && [ -f /root/shardora/shards3 ] && [ -n "${nodes_count:-}" ]; then
        head -n "$nodes_count" /root/shardora/shards3 > /root/shardora/shards2
    fi
}

shardora_genesis_bin() {
    local bin="/root/nodes/shardora/shardora"
    if [ ! -x "$bin" ]; then
        bin="/root/shardora/cbuild_${TARGET}/shardora"
    fi
    if [ ! -x "$bin" ]; then
        bin="$CODE_PATH/cbuild_${TARGET}/shardora"
    fi
    echo "$bin"
}

record_remote_failure() {
    echo "$1" >> "$REMOTE_FAIL_FILE"
}

check_remote_failures() {
    if [ -s "$REMOTE_FAIL_FILE" ]; then
        cat "$REMOTE_FAIL_FILE" >&2
        rm -f "$REMOTE_FAIL_FILE"
        exit 1
    fi
}

is_local_ip() {
    local ip="$1"
    if [ "$ip" = "localhost" ] || [ "$ip" = "127.0.0.1" ]; then
        return 0
    fi
    hostname -I 2>/dev/null | tr ' ' '\n' | grep -Fxq "$ip"
}

all_nodes_local() {
    local current_node_ips_array=(${node_ips//,/ })
    local ip
    for ip in "${current_node_ips_array[@]}"; do
        if ! is_local_ip "$ip"; then
            return 1
        fi
    done
    return 0
}

run_on_node() {
    local ip="$1"
    local command="$2"
    if is_local_ip "$ip"; then
        bash -lc "$command"
    else
        sshpass -p "$PASSWORD" ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 \
            root@$ip -p "$NODE_SSH_PORT" "$command"
    fi
}

copy_to_node() {
    local ip="$1"
    local src="$2"
    local dest="$3"
    if is_local_ip "$ip"; then
        cp -f "$src" "$dest"
    else
        sshpass -p "$PASSWORD" scp -P "$NODE_SSH_PORT" -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
            "$src" root@$ip:"$dest"
    fi
}

wait_remote_pids() {
    local status=0
    local pid
    for pid in "${REMOTE_PIDS[@]}"; do
        wait "$pid" || status=1
    done
    REMOTE_PIDS=()
    #check_remote_failures
    #if [ "$status" -ne 0 ]; then
    #    exit "$status"
    #fi
}

if is_container_env; then
    bash cmd.sh $2 "pkill -9 shardora 2>/dev/null || true; tc qdisc del dev eth0 root 2>/dev/null || true"
else
    bash cmd.sh $2 "sudo tc qdisc del dev eth0 root 2>/dev/null || true; pkill -9 shardora 2>/dev/null || true; systemctl list-units --type=service --all 'shardora@*' --no-legend 2>/dev/null | awk '{print \$1}' | while read -r u; do [ -n \"\$u\" ] && systemctl stop \"\$u\" 2>/dev/null; [ -n \"\$u\" ] && systemctl disable \"\$u\" 2>/dev/null; done; systemctl daemon-reload; systemctl reset-failed"
fi
init() {
    tmp_ips=(${node_ips//-/ })
    tmp_ips_len=(${#tmp_ips[*]})
    ip_max_idx=0
    if (($tmp_ips_len > 1)); then
        for tmp_ip_nodes in "${tmp_ips[@]}"; do
            ips_array=(${tmp_ip_nodes//,/ })
            first_ip=(${ips_array[0]})
            second_ip=(${ips_array[1]})

            start=$(($first_ip + 0))
            end=$(($second_ip + 0))
            for ((i=start; i<=end; i++)); do
                if ((i==end));then
                    new_ips+="192.168.$ip_max_idx.$i"
                else
                    new_ips+="192.168.$ip_max_idx.$i,"
                fi
            done

            new_ips+=","
            ip_max_idx=$(($ip_max_idx+1))
        done

        node_ips=$new_ips
        echo $node_ips
    else
        ips_array=(${node_ips//,/ })
        ips_len=(${#ips_array[*]})
        if (($ips_len == 2)); then
            first_ip=(${ips_array[0]})
            second_ip=(${ips_array[1]})
            first_ip_len=(${#first_ip})
            new_ips=""
            if (($first_ip_len<=6)); then
                start=$(($first_ip + 0))
                end=$(($second_ip + 0))
                for ((i=start; i<=end; i++)); do
                    if ((i==end));then
                        new_ips+="192.168.0.$i"
                    else
                        new_ips+="192.168.0.$i,"
                    fi
                done
                node_ips=$new_ips
                echo $node_ips
            fi
        fi
    fi

    if [ "$node_ips" == "" ]; then
        echo "just use local single node."
        node_ips='10.10.1.115'
    fi

    bash cmd.sh $node_ips "tc qdisc del dev eth0 root"  > /dev/null 2>&1 &
    if [ "$end_shard" == "" ]; then
        end_shard=3
    fi

    if [ "$PASSWORD" == "" ] && ! all_nodes_local; then
        echo "remote node password is required when node_host is not local" >&2
        exit 1
    fi
    export SHARDORA_REMOTE_PASSWORD="$PASSWORD"

    if [ "$TARGET" == "" ]; then
        TARGET=Debug
    fi

    if [ "$TARGET" == "Release" ]; then
        rm -rf ./pkgs
    fi

    killall -9 shardora 2>/dev/null || true
    killall -9 txcli 2>/dev/null || true

    bash build.sh shardora $TARGET
    run_privileged rm -rf /root/nodes
    run_privileged cp -rf ./nodes_local /root/nodes
    rm -rf /root/nodes/*/shardora /root/nodes/*/core* /root/nodes/*/log/* /root/nodes/*/*db* 2>/dev/null || true

    ensure_nodes_shardora_conf
    run_privileged cp -f "./cbuild_$TARGET/shardora" /root/nodes/shardora/shardora
    chmod +x /root/nodes/shardora/shardora
    if [[ "$each_nodes_count" -eq "" ]]; then
        each_nodes_count=4
    fi


    nodes_count=$(($nodes_count - $each_nodes_count + $FIRST_NODE_COUNT))
    seed_repo_shard_files
    shard3_node_count=0
    if [ -f /root/shardora/shards3 ]; then
        shard3_node_count=$(wc -l < /root/shardora/shards3 | tr -d ' ')
    fi
    if [ "$shard3_node_count" != "$nodes_count" ]; then
        echo "new shard nodes file will create."
        rm -rf /root/shardora/shards*
        rm -rf /root/shardora/init_accounts*
        seed_repo_shard_files
    fi

    echo "node count: " $nodes_count
    rm -rf /root/nodes/shardora/latest_blocks
}

apply_pkg_conf_placeholders() {
    local conf_path='/root/nodes/shardora/pkg/temp/conf/shardora.conf'
    if [ ! -f "$conf_path" ]; then
        echo "ERROR: missing $conf_path" >&2
        exit 1
    fi

    printf '%s' "$bootstrap" > /tmp/bootstrap_data.tmp
    local pybin=''
    for candidate in python3 /root/tools/python3.10/bin/python3 python3.10; do
        if command -v "$candidate" >/dev/null 2>&1; then
            pybin="$candidate"
            break
        fi
    done
    if [ -z "$pybin" ]; then
        echo "ERROR: python3 not found, cannot substitute BOOTSTRAP" >&2
        exit 1
    fi

    BOOTSTRAP_FILE=/tmp/bootstrap_data.tmp FOR_CK_VALUE=false "$pybin" - <<'PY'
import os
conf_path = '/root/nodes/shardora/pkg/temp/conf/shardora.conf'
with open(os.environ['BOOTSTRAP_FILE'], 'r', encoding='utf-8') as f:
    bootstrap_val = f.read()
with open(conf_path, 'r', encoding='utf-8') as f:
    content = f.read()
content = content.replace('BOOTSTRAP', bootstrap_val)
content = content.replace('FOR_CK_CLIENT', os.environ.get('FOR_CK_VALUE', 'false'))
with open(conf_path, 'w', encoding='utf-8') as f:
    f.write(content)
PY

    rm -f /tmp/bootstrap_data.tmp
    if grep -q 'BOOTSTRAP' "$conf_path" || grep -q 'FOR_CK_CLIENT' "$conf_path"; then
        echo "ERROR: placeholder substitution failed in $conf_path" >&2
        exit 1
    fi
}

sync_shards_for_genesis() {
    for ((shard_id=2; shard_id<=end_shard; shard_id++)); do
        if [ -f "/root/shardora/shards${shard_id}" ]; then
            cp -f "/root/shardora/shards${shard_id}" "/root/nodes/shardora/shards${shard_id}"
        fi
    done
}

# tx_cli reads plain 64-hex-char private keys; genesis funds both init_accounts
# and shards. Keep them identical and never ship encrypted init_accounts to pkg.
sync_init_accounts_from_shards() {
    for ((shard_id=2; shard_id<=end_shard; shard_id++)); do
        if [ -f "/root/shardora/shards${shard_id}" ]; then
            cp -f "/root/shardora/shards${shard_id}" "/root/shardora/init_accounts${shard_id}"
            if [ -d "/root/nodes/shardora" ]; then
                cp -f "/root/shardora/shards${shard_id}" "/root/nodes/shardora/init_accounts${shard_id}"
            fi
        fi
    done
}

copy_pkg_account_files() {
    local shardora_bin="/root/shardora/cbuild_${TARGET}/shardora"
    for ((shard_id=2; shard_id<=end_shard; shard_id++)); do
        if [ ! -f "/root/shardora/shards${shard_id}" ]; then
            echo "ERROR: missing /root/shardora/shards${shard_id}" >&2
            exit 1
        fi
        "$shardora_bin" -A "/root/shardora/shards${shard_id}" -D "/root/nodes/shardora/pkg/shards${shard_id}"
        cp -f "/root/shardora/shards${shard_id}" "/root/nodes/shardora/pkg/init_accounts${shard_id}"
    done
}

refresh_genesis_databases() {
    ensure_nodes_shardora_conf
    local genesis_bin
    genesis_bin="$(shardora_genesis_bin)"
    if [ ! -x "$genesis_bin" ]; then
        echo "ERROR: shardora binary not found for genesis refresh" >&2
        exit 1
    fi
    sync_shards_for_genesis
    sync_init_accounts_from_shards
    # Wipe stale genesis dbs so init_accounts/shards changes are written back.
    rm -rf /root/nodes/shardora/shard_db_* /root/nodes/shardora/root_db
    rm -f /root/nodes/shardora/root_blocks /root/nodes/shardora/latest_blocks
    echo "refresh genesis db: nodes=$nodes_count shards=2..$end_shard"
    cd /root/nodes/shardora && "$genesis_bin" -U -N "$nodes_count" -E 4
    cd /root/nodes/shardora && "$genesis_bin" -S 3 -N "$nodes_count" -E 4
    for ((shard_id=2; shard_id<=end_shard; shard_id++)); do
        if [ -f "/root/nodes/shardora/shards${shard_id}" ]; then
            cp -f "/root/nodes/shardora/shards${shard_id}" "/root/shardora/shards${shard_id}"
            cp -f "/root/shardora/shards${shard_id}" "/root/shardora/init_accounts${shard_id}"
        fi
    done
}

get_bootstrap() {
    bootstrap=""
    node_ips_array=(${node_ips//,/ })
    for ((shard_id=2; shard_id<=$end_shard; shard_id++)); do
        i=1
        for ip in "${node_ips_array[@]}"; do
            for ((j=0; j<$each_nodes_count;j++)); do
                local shard_file="/root/shardora/shards${shard_id}"
                if [ ! -f "$shard_file" ]; then
                    shard_file="/root/nodes/shardora/pkg/shards${shard_id}"
                fi
                tmppubkey=`sed -n "$i""p" "$shard_file" | awk -F'\t' '{print $2}'`
                if [ -z "$tmppubkey" ]; then
                    echo "ERROR: empty pubkey at line $i in $shard_file" >&2
                    exit 1
                fi
                port=''
                if ((i>=100)); then
                    port='1'$shard_id''$i
                elif ((i>=10)); then
                    port='1'$shard_id'0'$i
                else
                    port='1'$shard_id'00'$i
                fi

                if (( port > 65535 )); then
                    (( port = (port % 60000) + 1024 ))
                fi

                node_info=$tmppubkey":"$ip":"$port":"$shard_id
                if [ -z "$bootstrap" ]; then
                    bootstrap="$node_info"
                else
                    bootstrap="$bootstrap,$node_info"
                fi
                i=$((i+1))
            done
        done
    done

    if [ -z "$bootstrap" ]; then
        echo "ERROR: empty bootstrap list" >&2
        exit 1
    fi

    apply_pkg_conf_placeholders
    echo "$bootstrap"
}

make_package() {
    mkdir -p /root/shardora/pkgs
    rm -rf /root/nodes/shardora/pkg
    if [ -d "/root/shardora/pkgs/$node_hash" ]; then
        cd /root/shardora/ && bash build.sh shardora $TARGET
        cd /root/shardora/cbuild_$TARGET && make txcli
        cp -rf /root/shardora/cbuild_$TARGET/shardora /root/shardora/pkgs/$node_hash/shardora
        cp -rf /root/shardora/pkgs/$node_hash /root/nodes/shardora/pkg
        rm -rf /root/nodes/shardora/pkg/temp
        cp -rf /root/nodes/temp /root/nodes/shardora/pkg
        # Always refresh scripts so latest placeholder substitutions take effect.
        cp /root/shardora/temp_cmd.sh /root/nodes/shardora/pkg
        cp /root/shardora/start_cmd.sh /root/nodes/shardora/pkg
        copy_pkg_account_files
        refresh_genesis_databases
        cp -rf /root/nodes/shardora/shard_db_2 /root/nodes/shardora/pkg/shard_db_2
        cp -rf /root/nodes/shardora/shard_db_3 /root/nodes/shardora/pkg/
    else
        refresh_genesis_databases
        local genesis_bin
        genesis_bin="$(shardora_genesis_bin)"
        cd /root/nodes/shardora && "$genesis_bin" -C
        cd /root/shardora/cbuild_$TARGET && make txcli -j$(nproc 2>/dev/null || echo 4)

        mkdir -p /root/nodes/shardora/pkg
        cp /root/nodes/shardora/shardora /root/nodes/shardora/pkg
        cp /root/shardora/cbuild_$TARGET/txcli /root/nodes/shardora/pkg/txcli
        cp /root/nodes/shardora/conf/GeoLite2-City.mmdb /root/nodes/shardora/pkg
        cp /root/nodes/shardora/conf/log4cpp.properties /root/nodes/shardora/pkg
        copy_pkg_account_files
        cp /root/shardora/temp_cmd.sh /root/nodes/shardora/pkg
        cp /root/shardora/start_cmd.sh /root/nodes/shardora/pkg
        cp -rf /root/nodes/shardora/shard_db_2 /root/nodes/shardora/pkg/shard_db_2
        cp -rf /root/nodes/shardora/shard_db_3 /root/nodes/shardora/pkg/
        cp -rf /root/nodes/temp /root/nodes/shardora/pkg
        if [ -d /root/shardora/gdb ]; then
            cp -rf /root/shardora/gdb/* /root/nodes/shardora/pkg
        fi
        cp -rf /root/nodes/shardora/pkg /root/shardora/pkgs/$node_hash
    fi

    get_bootstrap
    cd /root/nodes/shardora/ && tar -zcvf pkg.tar.gz ./pkg > /dev/null 2>&1
}

check_cmd_finished() {
    echo "waiting..."
    wait_remote_pids
    echo "waiting ok"
}


clear_command() {
    echo 'clear_command start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    start_pos=1
    for ip in "${node_ips_array[@]}"; do
        (
            run_on_node "$ip" "cd /root && rm -rf pkg* && (killall -9 shardora 2>/dev/null || true)" ||
                record_remote_failure "clear command failed on $ip:$NODE_SSH_PORT"
        ) &
        REMOTE_PIDS+=($!)
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 250)); then
            check_cmd_finished
            run_cmd_count=0
        fi
        start_pos=$(($start_pos+$each_nodes_count))
    done

    check_cmd_finished
    echo 'clear_command over'
}

scp_package() {
    echo 'scp_package start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    for ip in "${node_ips_array[@]}"; do
        (
            copy_to_node "$ip" "/root/nodes/shardora/pkg.tar.gz" "/root/pkg.tar.gz" ||
                record_remote_failure "scp package failed on $ip:$NODE_SSH_PORT"
        ) &
        REMOTE_PIDS+=($!)
        run_cmd_count=$((run_cmd_count + 1))
        if (($run_cmd_count >= 100)); then
            check_cmd_finished
            run_cmd_count=0
        fi
    done

    check_cmd_finished
    echo 'scp_package over'
}

run_command() {
    echo 'run_command start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    start_pos=1
    for ip in "${node_ips_array[@]}"; do
        echo "run temp_cmd node: " $ip $each_nodes_count
        start_nodes_count=$(($each_nodes_count + 0))
        if ((start_pos==1)); then
            start_nodes_count=$FIRST_NODE_COUNT
        fi

        leader_init_tm=$(date -u -d "+240 days" +%s)
        (
            run_on_node "$ip" "cd /root && tar -zxvf pkg.tar.gz && cd ./pkg && bash temp_cmd.sh $ip $start_pos $start_nodes_count 0 2 $end_shard $leader_init_tm " ||
                record_remote_failure "temp command failed on $ip:$NODE_SSH_PORT"
        ) &
        REMOTE_PIDS+=($!)
        if ((start_pos==1)); then
            sleep 3
        fi

        run_cmd_count=$(($run_cmd_count + 1))
        if (($run_cmd_count >= 250)); then
            check_cmd_finished
            run_cmd_count=0
        fi
        start_pos=$(($start_pos+$start_nodes_count))
    done

    check_cmd_finished
    echo 'run_command over'
}

start_all_nodes() {
    echo 'start_all_nodes start'
    node_ips_array=(${node_ips//,/ })
    start_pos=1
    for ip in "${node_ips_array[@]}"; do
        echo "run start_cmd node: " $ip $each_nodes_count
        start_nodes_count=$(($each_nodes_count + 0))
        if ((start_pos==1)); then
            start_nodes_count=$FIRST_NODE_COUNT
        fi

        (
            run_on_node "$ip" "cd /root/pkg && bash start_cmd.sh $ip $start_pos $start_nodes_count 0 2 $end_shard " ||
                record_remote_failure "start command failed on $ip:$NODE_SSH_PORT"
        ) &
        REMOTE_PIDS+=($!)
        if ((start_pos==1)); then
            sleep 3
        fi

        sleep 0.1
        start_pos=$(($start_pos+$start_nodes_count))
    done

    check_cmd_finished
    echo 'start_all_nodes over'
}

init_mining_dir() {
    cd $CODE_PATH
    echo "init_mining_dir start..."
    local mining_path="./mining_node"
    rm -rf $mining_path
    mkdir -p $mining_path/conf
    mkdir -p $mining_path/log

    cp -rf /root/nodes/shardora/pkg/shard_db_3 $mining_path/db
    cp /root/nodes/shardora/pkg/GeoLite2-City.mmdb $mining_path/conf/
    cat <<EOF > $mining_path/conf/shardora.conf_temp
[db]
path = "./db"

[log]
path = "log/shardora.log"

[shardora]
bootstrap = ${bootstrap}
prikey = REPLACE_PRIVATE_KEY
local_ip = REPLACE_LOCAL_IP
public_ip = REPLACE_PUBLIC_IP
http_port = 24009
local_port = 14009
net_id = 3
leader_change_init_tm=0
tx_ws_ip = 0.0.0.0
tx_ws_port = 34009
EOF

    echo "Mining directory initialized at $mining_path"
}


killall -9 sshpass 2>/dev/null || true
init
make_package
clear_command
scp_package
# get_bootstrap
run_command
init_mining_dir
start_all_nodes
