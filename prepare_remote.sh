each_nodes_count=$1
node_ips=$2
bootstrap=""
end_shard=$3
PASSWORD=$4
TARGET=$5

init() {
    if [ "$node_ips" == "" ]; then
        echo "just use local single node."
        node_ips='127.0.0.1'
    fi  

    if [ "$end_shard" == "" ]; then
        end_shard=3
    fi  

    if [ "$PASSWORD" == "" ]; then
        PASSWORD="Xf4aGbTaf!"
    fi

    if [ "$TARGET" == "" ]; then
        TARGET=Release
    fi

    killall -9 shardora
    killall -9 txcli

    sudo rm -rf /root/shardoras
    sudo cp -rf ./shardoras_local /root/shardoras
    rm -rf /root/shardoras/*/shardora /root/shardoras/*/core* /root/shardoras/*/log/* /root/shardoras/*/*db*

    cp -rf ./shardoras_local/shardora/GeoLite2-City.mmdb /root/shardoras/shardora
    cp -rf ./shardoras_local/shardora/conf/log4cpp.properties /root/shardoras/shardora/conf
    mkdir -p /root/shardoras/shardora/log


    sudo cp -rf ./cbuild_$TARGET/shardora /root/shardoras/shardora
    if [[ "$each_nodes_count" -eq "" ]]; then
        each_nodes_count=4 
    fi

    node_ips_array=(${node_ips//,/ })
    nodes_count=0
    for ip in "${node_ips_array[@]}"; do
        nodes_count=$(($nodes_count + $each_nodes_count))
    done

    shard3_node_count=`wc -l /root/shardora/shards3 | awk -F' ' '{print $1}'`
    if [ "$shard3_node_count" != "$nodes_count" ]; then
        echo "new shard nodes file will create."
        rm -rf /root/shardora/shards*
    fi  

    echo "node count: " $nodes_count
    cd /root/shardoras/shardora && ./shardora -U -N $nodes_count
    cd /root/shardoras/shardora && ./shardora -S 3 -N $nodes_count

    rm -rf /root/shardoras/r*
    rm -rf /root/shardoras/s*
    rm -rf /root/shardoras/new*
    rm -rf /root/shardoras/node
    rm -rf /root/shardoras/param
}

make_package() {
    rm -rf /root/shardoras/shardora/pkg
    mkdir /root/shardoras/shardora/pkg
    cp /root/shardoras/shardora/shardora /root/shardoras/shardora/pkg
    cp /root/shardoras/shardora/conf/GeoLite2-City.mmdb /root/shardoras/shardora/pkg
    cp /root/shardoras/shardora/conf/log4cpp.properties /root/shardoras/shardora/pkg
    cp /root/shardora/shards3 /root/shardoras/shardora/pkg
    cp /root/shardora/root_nodes /root/shardoras/shardora/pkg/shards2
    cp /root/shardora/temp_cmd.sh /root/shardoras/shardora/pkg
    cp /root/shardora/start_cmd.sh /root/shardoras/shardora/pkg
    cp -rf /root/shardoras/shardora/root_db /root/shardoras/shardora/pkg/shard_db_2
    cp -rf /root/shardoras/shardora/shard_db_3 /root/shardoras/shardora/pkg
    cp -rf /root/shardoras/temp /root/shardoras/shardora/pkg
    cd /root/shardoras/shardora/ && tar -zcvf pkg.tar.gz ./pkg > /dev/null 2>&1
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

    ps -ef | grep sshpass | grep ConnectTimeout
    echo "waiting ok"
}


clear_command() {
    echo 'run_command start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    start_pos=1
    for ip in "${node_ips_array[@]}"; do 
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && rm -rf pkg*" &
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 1)); then
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
        sshpass -p $PASSWORD scp -o StrictHostKeyChecking=no /root/shardoras/shardora/pkg.tar.gz root@$ip:/root &
        run_cmd_count=$((run_cmd_count + i))
        if [ $run_cmd_count -ge 10 ]; then
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
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && tar -zxvf pkg.tar.gz > /dev/null 2>&1 && cd ./pkg && sh temp_cmd.sh $ip $start_pos $each_nodes_count $bootstrap 2 $end_shard" &
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 100)); then
            check_cmd_finished
            run_cmd_count=0
        fi
        start_pos=$(($start_pos+$each_nodes_count))
    done

    check_cmd_finished
    echo 'run_command over'
}

start_all_nodes() {
    echo 'start_all_nodes start'
    node_ips_array=(${node_ips//,/ })
    run_cmd_count=0
    start_pos=1
    for ip in "${node_ips_array[@]}"; do
        echo "start node: " $ip 
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && tar -zxvf pkg.tar.gz > /dev/null 2>&1 && cd ./pkg && sh start_cmd.sh $ip $start_pos $each_nodes_count $bootstrap 2 $end_shard &"  &
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 100)); then
            check_cmd_finished
            run_cmd_count=0
        fi
        start_pos=$(($start_pos+$each_nodes_count))
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
