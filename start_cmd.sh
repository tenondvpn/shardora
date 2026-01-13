local_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
start_shard=$5
end_shard=$6

echo "new node: $local_ip $start_pos $node_count $start_shard $end_shard"

#!/bin/bash

# 1. 备份配置文件
cp /etc/sysctl.conf /etc/sysctl.conf.bak
cp /etc/security/limits.conf /etc/security/limits.conf.bak


# 2. 提高系统级和用户级文件描述符限制 (P2P需要大量连接)
cat >> /etc/security/limits.conf <<EOF
* soft nofile 1000000
* hard nofile 1000000
* soft nproc 32768
* hard nproc 32768
root soft nofile 1000000
root hard nofile 1000000
EOF

# 临时生效
ulimit -n 1000000


# 3. 写入内核优化参数
cat >> /etc/sysctl.conf <<EOF

# --- P2P & 高并发优化 ---

# 增加系统最大打开文件数
fs.file-max = 1000000

# 开启 BBR (如果内核支持)
net.core.default_qdisc = fq
net.ipv4.tcp_congestion_control = bbr

# 允许重用 TIME-WAIT sockets，加快回收
net.ipv4.tcp_tw_reuse = 1
# 缩短 FIN_WAIT_2 时间
net.ipv4.tcp_fin_timeout = 30
# 缩短 Keepalive 探测时间 (默认7200秒太长)
net.ipv4.tcp_keepalive_time = 1200

# 扩大 TCP 缓冲区 (提升大带宽下的速度)
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.ipv4.tcp_rmem = 4096 87380 16777216
net.ipv4.tcp_wmem = 4096 65536 16777216

# 增加连接追踪上限 (防止 P2P 导致丢包)
# 注意：如果服务器内存小于 2GB，请适当调小此值
net.netfilter.nf_conntrack_max = 1000000
net.nf_conntrack_max = 1000000

# 增加 SYN 队列长度，防止突发流量导致无法连接
net.ipv4.tcp_max_syn_backlog = 8192
net.core.somaxconn = 8192

# 应对 SYN 洪水攻击
net.ipv4.tcp_syncookies = 1

# 端口范围扩大
net.ipv4.ip_local_port_range = 10000 65000

# 禁用慢启动空闲重启
net.ipv4.tcp_slow_start_after_idle = 0
EOF

# 4. 应用更改
sysctl -p

start_nodes() {
    end_pos=$(($start_pos + $node_count - 1))
    for ((shard_id=$start_shard; shard_id<=$end_shard; shard_id++)); do
        shard_node_count=`wc -l /root/pkg/shards$shard_id | awk -F' ' '{print $1}'`
        echo /root/pkg/shards$shard_id $shard_node_count
        for ((i=$start_pos; i<=$end_pos;i++)); do
            if (($i > $shard_node_count));then
                break
            fi

            cd /root/zjnodes/s$shard_id'_'$i/ && ulimit -c unlimited; ulimit -n 1024000 && nohup ./shardora -f 0 -g 0 s$shard_id'_'$i &
            if ((shard_id==2 && i==start_pos)); then
                sleep 3
            fi
            sleep 0.5
        done
    done
}

killall -9 shardora
start_nodes
