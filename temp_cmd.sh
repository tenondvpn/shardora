public_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
start_shard=$5
end_shard=$6
leader_init_tm=$7
TEST_TX_TPS=5000
TEST_TX_MAX_POOL_INDEX=1

echo "new node: $public_ip $start_pos $node_count $start_shard $end_shard"
rm -rf /root/shardoras/
mkdir -p /root/shardoras/

local_ip=`hostname -I | awk '{print $1}'`

# ========== 网络模拟配置 ==========
# 注意: 不使用 TC 层网络模拟，因为会导致 TCP 报文破坏
# 应用层延迟注入通过环境变量配置，在 Transport 层实现
# 
# 使用方式:
#   export SHARDORA_NETWORK_ENABLED=1
#   export SHARDORA_NETWORK_DELAY_MS=25
#   export SHARDORA_NETWORK_JITTER_MS=10
#   export SHARDORA_NETWORK_LOSS_RATE=0.0001
#   ./temp_cmd.sh <params>
# ========== 网络模拟配置结束 ==========
deploy_nodes() {
    end_pos=$(($start_pos + $node_count - 1))
    for ((shard_id=$start_shard; shard_id<=$end_shard; shard_id++)); do
        shard_node_count=`wc -l /root/pkg/shards$shard_id | awk -F' ' '{print $1}'`
        ls /root/pkg/shards$shard_id
        echo /root/pkg/shards$shard_id $shard_node_count
        for ((i=$start_pos; i<=$end_pos;i++)); do
            if (($i > $shard_node_count)); then
                break
            fi

            prikey=`sed -n "$i""p" /root/pkg/shards$shard_id | awk -F'\t' '{print $1}'`
            pubkey=`sed -n "$i""p" /root/pkg/shards$shard_id | awk -F'\t' '{print $2}'`

            # 支持重复执行：如果目录已存在，先删除
            if [ -d "/root/shardoras/s$shard_id'_'$i" ]; then
                echo "节点 s$shard_id'_'$i 已存在，删除旧配置..."
                rm -rf "/root/shardoras/s$shard_id'_'$i"
            fi

            cp -rf /root/pkg/temp /root/shardoras/s$shard_id'_'$i
            conf=/root/shardoras/s$shard_id'_'$i/conf/shardora.conf
            sed -i 's/PRIVATE_KEY/'$prikey'/g' "$conf"
            sed -i 's/PUBLIC_IP/'$public_ip'/g' "$conf"
            sed -i 's/LOCAL_IP/'$local_ip'/g' "$conf"
            sed -i 's/LEADER_CHANGE_INIT_TM/'$leader_init_tm'/g' "$conf"

            port0=''
            port1=''
            port2=''
            if ((i>=100)); then
                port0='1'$shard_id''$i
                port1='2'$shard_id''$i
                port2='3'$shard_id''$i
            elif ((i>=10)); then
                port0='1'$shard_id'0'$i
                port1='2'$shard_id'0'$i
                port2='3'$shard_id'0'$i
            else
                port0='1'$shard_id'00'$i
                port1='2'$shard_id'00'$i
                port2='3'$shard_id'00'$i
            fi

            if (( port0 > 65535 )); then
                (( port0 = (port0 % 60000) + 1024 ))
            fi

            if (( port1 > 65535 )); then
                (( port1 = (port1 % 60000) + 1024 ))
            fi

            if (( port2 > 65535 )); then
                (( port2 = (port2 % 60000) + 1024 ))
            fi

            sed -i 's/HTTP_PORT/'$port1'/g' "$conf"
            sed -i 's/LOCAL_PORT/'$port0'/g' "$conf"
            sed -i 's/TX_WS_PORT/'$port2'/g' "$conf"
            if ((shard_id==3)); then
                if ((i<=TEST_TX_MAX_POOL_INDEX)); then
                    sed -i 's/TEST_POOL_INDEX/'$(($i-1))'/g' "$conf"
                else
                    sed -i 's/TEST_POOL_INDEX/-1/g' "$conf"
                fi
                sed -i 's/TEST_TX_TPS/'$TEST_TX_TPS'/g' "$conf"
            fi
            sed -i 's/FOR_CK_CLIENT/false/g' "$conf"
            if grep -qE 'BOOTSTRAP|FOR_CK_CLIENT|PRIVATE_KEY|LOCAL_IP|PUBLIC_IP|HTTP_PORT|LOCAL_PORT|TX_WS_PORT' "$conf"; then
                echo "FATAL: unset placeholders remain in $conf" >&2
                exit 1
            fi

            echo /root/shardoras/s$shard_id'_'$i/shardora

            # 支持重复执行：删除旧的符号链接
            rm -f /root/shardoras/s$shard_id'_'$i/shardora
            rm -f /root/shardoras/s$shard_id'_'$i/txcli
            rm -f /root/shardoras/s$shard_id'_'$i/conf/GeoLite2-City.mmdb
            rm -f /root/shardoras/s$shard_id'_'$i/conf/log4cpp.properties

            ln /root/pkg/shardora /root/shardoras/s$shard_id'_'$i/shardora
            if [[ -f /root/pkg/txcli ]]; then
                ln /root/pkg/txcli /root/shardoras/s$shard_id'_'$i/txcli
            fi
            cp -rf /root/pkg/init_accounts* /root/shardoras/s$shard_id'_'$i/
            if [[ -f /root/pkg/GeoLite2-City.mmdb ]]; then
                ln /root/pkg/GeoLite2-City.mmdb /root/shardoras/s$shard_id'_'$i/conf/GeoLite2-City.mmdb
            fi
            if [[ -f /root/pkg/log4cpp.properties ]]; then
                ln /root/pkg/log4cpp.properties /root/shardoras/s$shard_id'_'$i/conf/log4cpp.properties
            fi
            mkdir -p /root/shardoras/s$shard_id'_'$i/log

            # 支持重复执行：删除旧的数据库
            rm -rf /root/shardoras/s$shard_id'_'$i/db
            cp -rf /root/pkg/shard_db_$shard_id /root/shardoras/s$shard_id'_'$i/db

            # Generate self-signed SSL certificate for HTTPS server
            echo "Generating SSL certificate for node s$shard_id'_'$i"
            # 删除旧证书
            rm -f /root/shardoras/s$shard_id'_'$i/server-key.pem
            rm -f /root/shardoras/s$shard_id'_'$i/server-cert.pem

            openssl req -x509 -newkey rsa:2048 -nodes \
                -keyout /root/shardoras/s$shard_id'_'$i/server-key.pem \
                -out /root/shardoras/s$shard_id'_'$i/server-cert.pem \
                -days 365 \
                -subj "/C=CN/ST=State/L=City/O=Shardora/OU=Node/CN=$local_ip" \
                2>/dev/null
            chmod 600 /root/shardoras/s$shard_id'_'$i/server-key.pem
            chmod 644 /root/shardoras/s$shard_id'_'$i/server-cert.pem
        done
    done
}

killall -9 shardora

rm -rf /tmp/asan*
# core 文件落在实例工作目录（/root/shardoras/<instance>/）
if [ -w /proc/sys/kernel/core_pattern ] 2>/dev/null; then
    cat > /etc/sysctl.d/99-shardora-coredump.conf <<'EOF'
fs.suid_dumpable = 1
kernel.core_pattern = core.%e.%p
EOF
    sysctl -p /etc/sysctl.d/99-shardora-coredump.conf 2>/dev/null || true
fi
ulimit -c unlimited

deploy_nodes

# ========== 清除网络模拟配置 ==========
# 如果需要清除网络模拟，可以运行:
# tc qdisc del dev <interface> root
# 或者在脚本中添加参数 "cleanup" 来自动清除
if [ "$9" = "cleanup" ]; then
    main_interface=$(get_main_interface)
    if [ ! -z "$main_interface" ]; then
        echo "清除网络模拟配置: $main_interface"
        tc qdisc del dev "$main_interface" root 2>/dev/null || true
        echo "✓ 网络模拟配置已清除"
    fi
fi
# ========== 清除网络模拟配置结束 ==========