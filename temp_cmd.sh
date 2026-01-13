local_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
start_shard=$5
end_shard=$6
TEST_TX_TPS=5000
TEST_TX_MAX_POOL_INDEX=1

echo "new node: $local_ip $start_pos $node_count $start_shard $end_shard"
rm -rf /root/zjnodes/
mkdir -p /root/zjnodes/


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
            sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
            sed -i 's/LOCAL_IP/'$local_ip'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
            sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
            if ((i<=TEST_TX_MAX_POOL_INDEX)); then
                sed -i 's/TEST_POOL_INDEX/'$(($i-1))'/g' /root/zjnodes/s3_$i/conf/shardora.conf
            else
                sed -i 's/TEST_POOL_INDEX/-1/g' /root/zjnodes/s3_$i/conf/shardora.conf
            fi

            sed -i 's/TEST_TX_TPS/'$TEST_TX_TPS'/g' /root/zjnodes/s3_$i/conf/shardora.conf

            if ((i>=100)); then
                sed -i 's/HTTP_PORT/2'$shard_id''$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
                sed -i 's/LOCAL_PORT/1'$shard_id''$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
            elif ((i>=10)); then
                sed -i 's/HTTP_PORT/2'$shard_id'0'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
                sed -i 's/LOCAL_PORT/1'$shard_id'0'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf 
            else
                sed -i 's/HTTP_PORT/2'$shard_id'00'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf
                sed -i 's/LOCAL_PORT/1'$shard_id'00'$i'/g' /root/zjnodes/s$shard_id'_'$i/conf/shardora.conf 
            fi

            echo /root/zjnodes/s$shard_id'_'$i/shardora
            ln /root/pkg/shardora /root/zjnodes/s$shard_id'_'$i/shardora
            ln /root/pkg/GeoLite2-City.mmdb /root/zjnodes/s$shard_id'_'$i/conf/GeoLite2-City.mmdb
            ln /root/pkg/log4cpp.properties /root/zjnodes/s$shard_id'_'$i/conf/log4cpp.properties
            mkdir -p /root/zjnodes/s$shard_id'_'$i/log
            cp -rf /root/pkg/shard_db_$shard_id /root/zjnodes/s$shard_id'_'$i/db
        done
    done
}


killall -9 shardora

deploy_nodes
