local_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
start_shard=$5
end_shard=$6

echo "new node: $local_ip $start_pos $node_count $start_shard $end_shard"

start_nodes() {
    end_pos=$(($start_pos + $node_count - 1))
    for ((shard_id=$start_shard; shard_id<=$end_shard; shard_id++)); do
        shard_node_count=`wc -l /root/pkg/shards$shard_id | awk -F' ' '{print $1}'`
        echo /root/pkg/shards$shard_id $shard_node_count
        for ((i=$start_pos; i<=$end_pos;i++)); do
            if (($i > $shard_node_count));then
                break
            fi

            cd /root/zjnodes/s$shard_id'_'$i/ && ulimit -c unlimited && nohup ./shardora -f 0 -g 0 s$shard_id'_'$i &
            if ((shard_id==2 && i==start_pos)); then
                sleep 3
            fi
            sleep 0.5
        done
    done
}

killall -9 shardora
start_nodes
