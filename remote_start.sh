each_nodes_count=$1
node_ips=$2
bootstrap=""
end_shard=$3
PASSWORD=$4
TARGET=$5
FIRST_NODE_COUNT=10

init() {
    killall -9 zjchain
    killall -9 txcli
    rm -rf /root/zjnodes/r*
    rm -rf /root/zjnodes/s*
    rm -rf /root/zjnodes/new*
    rm -rf /root/zjnodes/node
    rm -rf /root/zjnodes/param
    if [ "$TARGET" == "" ]; then
        TARGET=Release
    fi
}

make_package() {
    rm -rf /root/zjnodes/zjchain/pkg*
    cp -rf /root/shardora/sshpass /usr/bin/
    cd /root/shardora && tar -zxvf pkg.tar.gz
    cp -rf /root/shardora/cbuild_$TARGET/zjchain /root/shardora/pkg/
    cd /root/shardora/ && tar -zcvf pkg.tar.gz ./pkg > /dev/null 2>&1 
}

get_bootstrap() {
    rm -rf /root/shardora/shards2
    cp -rf /root/shardora/root_nodes /root/shardora/shards2
    node_ips_array=(${node_ips//,/ })
    for ((shard_id=2; shard_id<=$end_shard; shard_id++)); do
        i=1
        for ip in "${node_ips_array[@]}"; do 
            tmppubkey=`sed -n "$i""p" /root/shardora/shards$shard_id| awk -F'\t' '{print $2}'`
            node_info=$tmppubkey":"$ip":1"$shard_id"00"$i
            bootstrap=$node_info","$bootstrap
            i=$((i+1))
            if ((i>=10)); then
                break
            fi
        done
    done
}

check_cmd_finished() {
    echo "waiting..."
    sleep 1
    ps -ef | grep sshpass 
    while true
    do
        sshpass_count=`ps -ef | grep sshpass | grep ConnectTimeout | wc -l`
        if [ "$sshpass_count" == "0" ]; then
            break
        fi
        sleep 1
    done

    ps -ef | grep sshpass
    echo "waiting ok"
}


clear_command() {
    echo 'run_command start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    start_pos=1
    for ip in "${node_ips_array[@]}"; do 
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "rm -rf /root/pkg*; killall -9 zjchain; rm -rf /root/zjnodes/*" &
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 30)); then
            check_cmd_finished
            run_cmd_count=0
        fi
        start_pos=$(($start_pos+$each_nodes_count))
    done

    check_cmd_finished
    echo 'run_command over'
}

scp_package() {
    echo 'scp_package start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    for ip in "${node_ips_array[@]}"; do 
        sshpass -p $PASSWORD scp -o ConnectTimeout=10  -o StrictHostKeyChecking=no /root/shardora/pkg.tar.gz root@$ip:/root &
        run_cmd_count=$((run_cmd_count + 1))
        if (($run_cmd_count >= 5)); then
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
        echo "start node: " $ip $each_nodes_count
        start_nodes_count=$(($each_nodes_count + 0))
        if ((start_pos==1)); then
            start_nodes_count=$FIRST_NODE_COUNT
        fi

        if ((start_pos + start_nodes_count > 1024)); then
            start_nodes_count=$((1024 - $start_pos))
        fi

        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && tar -zxvf pkg.tar.gz > /dev/null 2>&1 && cd ./pkg && sh temp_cmd.sh $ip $start_pos $start_nodes_count $bootstrap 2 $end_shard" &
        if ((start_pos==1)); then
            sleep 3
        fi

        run_cmd_count=$(($run_cmd_count + 1))
        if (($run_cmd_count >= 30)); then
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
        echo "start node: " $ip $each_nodes_count
        start_nodes_count=$(($each_nodes_count + 0))
        if ((start_pos==1)); then
            start_nodes_count=$FIRST_NODE_COUNT
        fi

        if ((start_pos + start_nodes_count > 1024)); then
            start_nodes_count=$((1024 - $start_pos))
        fi
        
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && tar -zxvf pkg.tar.gz > /dev/null 2>&1 && cd ./pkg && sh start_cmd.sh $ip $start_pos $start_nodes_count $bootstrap 2 $end_shard &"  &
        if ((start_pos==1)); then
            sleep 3
        fi

        sleep 1
        start_pos=$(($start_pos+$start_nodes_count))
    done

    check_cmd_finished
    echo 'start_all_nodes over'
}

killall -9 sshpass
init
make_package
clear_command
scp_package
get_bootstrap
echo $bootstrap
run_command
start_all_nodes
