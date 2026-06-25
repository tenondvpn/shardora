#!/usr/bin/env bash
# Same contract as temp_cmd.sh (for Docker linux/arm64, e.g. Mac Docker Desktop).
# Fixes directory naming to s${shard_id}_${i} and avoids writes to kernel tunables
# when running unprivileged.

set -euo pipefail

public_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
start_shard=$5
end_shard=$6
leader_init_tm=$7
TEST_TX_TPS=5000
TEST_TX_MAX_POOL_INDEX=1

echo "new node (docker): $public_ip $start_pos $node_count $start_shard $end_shard"
rm -rf /root/shardoras/
mkdir -p /root/shardoras/

if command -v hostname >/dev/null 2>&1 && hostname -I >/dev/null 2>&1; then
    local_ip=$(hostname -I | awk '{print $1}')
else
    local_ip=$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}' || true)
fi
if [ -z "${local_ip:-}" ]; then
    local_ip="127.0.0.1"
fi

deploy_nodes() {
    end_pos=$(($start_pos + $node_count - 1))
    for ((shard_id=$start_shard; shard_id<=$end_shard; shard_id++)); do
        shard_node_count=$(wc -l "/root/pkg/shards${shard_id}" | awk '{print $1}')
        ls "/root/pkg/shards${shard_id}"
        echo "/root/pkg/shards${shard_id} $shard_node_count"
        for ((i=$start_pos; i<=$end_pos; i++)); do
            if ((i > shard_node_count)); then
                break
            fi

            prikey=$(sed -n "${i}p" "/root/pkg/shards${shard_id}" | awk -F'\t' '{print $1}')
            pubkey=$(sed -n "${i}p" "/root/pkg/shards${shard_id}" | awk -F'\t' '{print $2}')

            inst_dir="/root/shardoras/s${shard_id}_${i}"
            if [ -d "$inst_dir" ]; then
                echo "节点 s${shard_id}_${i} 已存在，删除旧配置..."
                rm -rf "$inst_dir"
            fi

            cp -rf /root/pkg/temp "$inst_dir"
            sed -i "s/PRIVATE_KEY/${prikey}/g" "$inst_dir/conf/shardora.conf"
            sed -i "s/PUBLIC_IP/${public_ip}/g" "$inst_dir/conf/shardora.conf"
            sed -i "s/LOCAL_IP/${local_ip}/g" "$inst_dir/conf/shardora.conf"
            sed -i "s/LEADER_CHANGE_INIT_TM/${leader_init_tm}/g" "$inst_dir/conf/shardora.conf"
            if [ "$shard_id" -eq 3 ]; then
                if ((i <= TEST_TX_MAX_POOL_INDEX)); then
                    sed -i "s/TEST_POOL_INDEX/$(($i - 1))/g" "$inst_dir/conf/shardora.conf"
                else
                    sed -i "s/TEST_POOL_INDEX/-1/g" "$inst_dir/conf/shardora.conf"
                fi
                sed -i "s/TEST_TX_TPS/${TEST_TX_TPS}/g" "$inst_dir/conf/shardora.conf"
            fi

            port0=''
            port1=''
            port2=''
            if ((i >= 100)); then
                port0="1${shard_id}${i}"
                port1="2${shard_id}${i}"
                port2="3${shard_id}${i}"
            elif ((i >= 10)); then
                port0="1${shard_id}0${i}"
                port1="2${shard_id}0${i}"
                port2="3${shard_id}0${i}"
            else
                port0="1${shard_id}00${i}"
                port1="2${shard_id}00${i}"
                port2="3${shard_id}00${i}"
            fi

            if ((port0 > 65535)); then
                ((port0 = (port0 % 60000) + 1024))
            fi
            if ((port1 > 65535)); then
                ((port1 = (port1 % 60000) + 1024))
            fi
            if ((port2 > 65535)); then
                ((port2 = (port2 % 60000) + 1024))
            fi

            sed -i "s/HTTP_PORT/${port1}/g" "$inst_dir/conf/shardora.conf"
            sed -i "s/LOCAL_PORT/${port0}/g" "$inst_dir/conf/shardora.conf"
            sed -i "s/TX_WS_PORT/${port2}/g" "$inst_dir/conf/shardora.conf"

            echo "${inst_dir}/shardora"

            rm -f "$inst_dir/shardora" "$inst_dir/txcli"
            rm -f "$inst_dir/conf/GeoLite2-City.mmdb" "$inst_dir/conf/log4cpp.properties"

            ln /root/pkg/shardora "$inst_dir/shardora"
            if [[ -f /root/pkg/txcli ]]; then
                ln /root/pkg/txcli "$inst_dir/txcli"
            fi
            cp -rf /root/pkg/init_accounts* "$inst_dir/" 2>/dev/null || true
            if [[ -f /root/pkg/GeoLite2-City.mmdb ]]; then
                ln /root/pkg/GeoLite2-City.mmdb "$inst_dir/conf/GeoLite2-City.mmdb"
            fi
            if [[ -f /root/pkg/log4cpp.properties ]]; then
                ln /root/pkg/log4cpp.properties "$inst_dir/conf/log4cpp.properties"
            fi
            mkdir -p "$inst_dir/log"

            rm -rf "$inst_dir/db"
            cp -rf "/root/pkg/shard_db_${shard_id}" "$inst_dir/db"

            echo "Generating SSL certificate for node s${shard_id}_${i}"
            rm -f "$inst_dir/server-key.pem" "$inst_dir/server-cert.pem"
            openssl req -x509 -newkey rsa:2048 -nodes \
                -keyout "$inst_dir/server-key.pem" \
                -out "$inst_dir/server-cert.pem" \
                -days 365 \
                -subj "/C=CN/ST=State/L=City/O=Shardora/OU=Node/CN=$local_ip" \
                2>/dev/null || true
            chmod 600 "$inst_dir/server-key.pem" 2>/dev/null || true
            chmod 644 "$inst_dir/server-cert.pem" 2>/dev/null || true
        done
    done
}

# Do not use pkill -f shardora: command lines under …/shardora/… would match and kill this script.
pkill -9 shardora 2>/dev/null || true
rm -rf /tmp/asan* 2>/dev/null || true
if [ -w /proc/sys/kernel/core_pattern ] 2>/dev/null; then
    echo "core.%e.%p" > /proc/sys/kernel/core_pattern 2>/dev/null || true
fi
ulimit -c unlimited 2>/dev/null || true

deploy_nodes
