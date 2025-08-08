local_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
start_shard=$5
end_shard=$6
TEST_TX_TPS=1000
TEST_TX_MAX_POOL_INDEX=0

echo "new node: $local_ip $start_pos $node_count $start_shard $end_shard"
rm -rf /root/zjnodes/
mkdir -p /root/zjnodes/

init_config() {
    echo "fs.file-max = 1024000" >> /etc/sysctl.conf
    echo "net.core.rmem_max = 67108864" >> /etc/sysctl.conf
    echo "net.core.wmem_max = 67108864" >> /etc/sysctl.conf
    echo "net.core.rmem_default = 65536" >> /etc/sysctl.conf
    echo "net.core.wmem_default = 65536" >> /etc/sysctl.conf
    echo "net.core.netdev_max_backlog = 4096" >> /etc/sysctl.conf
    echo "net.core.somaxconn = 409600" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_syncookies = 1" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_tw_reuse = 1" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_tw_recycle = 0" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_fin_timeout = 30" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_keepalive_time = 1200" >> /etc/sysctl.conf
    echo "net.ipv4.ip_local_port_range = 10000 65000" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_max_syn_backlog = 4096" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_max_tw_buckets = 5000" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_rmem = 4096 87380 67108864" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_wmem = 4096 65536 67108864" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_mtu_probing = 1" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_congestion_control = hybla" >> /etc/sysctl.conf
    echo "net.ipv4.ip_forward = 1" >> /etc/sysctl.conf
    echo "net.ipv4.tcp_fastopen = 3" >> /etc/sysctl.conf

    echo "*               soft    nofile           5120000" >> /etc/security/limits.conf
    echo "*               hard    nofile          10240000" >> /etc/security/limits.conf

    echo "session required pam_limits.so" >> /etc/pam.d/common-session

    echo "ulimit -SHn 1024000" >> /etc/profile
    # yum install -y wondershaper

#    cd /root/pkg && rpm -ivh gdb-7.6.1-120.el7.x86_64.rpm
}

init_firewall() {
    iptables -I FORWARD -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu
    tc qdisc del dev eth0 root
    tc qdisc add dev eth0 root netem delay 25ms
    # /root/pkg/wondershaper eth0 500000 500000
}

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
            cp -rf /root/pkg/temp /root/zjnodes/s$shard_id'_'$i
            sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
            sed -i 's/LOCAL_IP/'$local_ip'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
            sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
            if ((i<=TEST_TX_MAX_POOL_INDEX)); then
                sed -i 's/TEST_POOL_INDEX/'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
            fi
            sed -i 's/TEST_TX_TPS/'$TEST_TX_TPS'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
            if ((i>=100)); then
                sed -i 's/HTTP_PORT/2'$shard_id''$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
                sed -i 's/LOCAL_PORT/1'$shard_id''$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
            elif ((i>=10)); then
                sed -i 's/HTTP_PORT/2'$shard_id'0'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
                sed -i 's/LOCAL_PORT/1'$shard_id'0'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf 
            else
                sed -i 's/HTTP_PORT/2'$shard_id'00'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf
                sed -i 's/LOCAL_PORT/1'$shard_id'00'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/zjchain.conf 
            fi

            echo /root/zjnodes/s$shard_id'_'$i/zjchain
            ln /root/pkg/zjchain /root/zjnodes/s$shard_id'_'$i/zjchain
            ln /root/pkg/GeoLite2-City.mmdb /root/zjnodes/s$shard_id'_'$i/conf/GeoLite2-City.mmdb
            ln /root/pkg/log4cpp.properties /root/zjnodes/s$shard_id'_'$i/conf/log4cpp.properties
            mkdir -p /root/zjnodes/s$shard_id'_'$i/log
            cp -rf /root/pkg/shard_db_$shard_id /root/zjnodes/s$shard_id'_'$i/db
        done
    done
}

start_nodes() {
    end_pos=$(($start_pos + $node_count - 1))
    for ((shard_id=$start_shard; shard_id<=$end_shard; shard_id++)); do
        shard_node_count=`wc -l /root/pkg/shards$shard_id | awk -F' ' '{print $1}'`
        echo /root/pkg/shards$shard_id $shard_node_count
        for ((i=$start_pos; i<=$end_pos;i++)); do
            if (($i > $shard_node_count));then
                break
            fi

            cd /root/zjnodes/s$shard_id'_'$i/ && nohup ./zjchain -f 0 -g 0 s$shard_id'_'$i &
            if ((shard_id==2 && i==start_pos)); then
                sleep 3
            fi
            sleep 0.5
        done
    done
}

killall -9 zjchain

init_config
sudo sysctl -p
ulimit -c unlimited
init_firewall
deploy_nodes
# start_nodes
