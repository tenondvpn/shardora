each_nodes_count=$1
node_ips=$2
bootstrap=""
end_shard=$3
PASSWORD=$4
TARGET=$5
FIRST_NODE_COUNT=$1

init() {
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

    if [ "$node_ips" == "" ]; then
        echo "just use local single node."
        node_ips='127.0.0.1'
    fi  

    sh cmd.sh $node_ips "tc qdisc del dev eth0 root"  > /dev/null 2>&1 &
    if [ "$end_shard" == "" ]; then
        end_shard=3
    fi  

    if [ "$PASSWORD" == "" ]; then
        PASSWORD="Xf4aGbTaf!"
    fi

    if [ "$TARGET" == "" ]; then
        TARGET=Release
    fi

    killall -9 zjchain
    killall -9 txcli

    # sh build.sh a $TARGET
    # sudo rm -rf /root/zjnodes
    # sudo cp -rf ./zjnodes_local /root/zjnodes
    # rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

    # cp -rf ./zjnodes_local/zjchain/GeoLite2-City.mmdb /root/zjnodes/zjchain
    # cp -rf ./zjnodes_local/zjchain/conf/log4cpp.properties /root/zjnodes/zjchain/conf
    # mkdir -p /root/zjnodes/zjchain/log


    # sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
    # sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml

    # sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
    if [[ "$each_nodes_count" -eq "" ]]; then
        each_nodes_count=4 
    fi

    node_ips_array=(${node_ips//,/ })
    # nodes_count=0
    # for ip in "${node_ips_array[@]}"; do
    #     nodes_count=$(($nodes_count + $each_nodes_count))
    # done

    # nodes_count=$(($nodes_count - $each_nodes_count + $FIRST_NODE_COUNT))
    # shard3_node_count=`wc -l /root/shardora/shards3 | awk -F' ' '{print $1}'`
    # if [ "$shard3_node_count" != "$nodes_count" ]; then
    #     echo "new shard nodes file will create."
    #     rm -rf /root/shardora/shards*
    # fi  

    # echo "node count: " $nodes_count
    # cd /root/zjnodes/zjchain && ./zjchain -U -N $nodes_count
    # cd /root/zjnodes/zjchain && ./zjchain -S 3 -N $nodes_count

    # rm -rf /root/zjnodes/r*
    # rm -rf /root/zjnodes/s*
    # rm -rf /root/zjnodes/new*
    # rm -rf /root/zjnodes/node
    # rm -rf /root/zjnodes/param
}

make_package() {
    rm -rf /root/zjnodes/zjchain/pkg
    mkdir /root/zjnodes/zjchain/pkg
    cp /root/zjnodes/zjchain/zjchain /root/zjnodes/zjchain/pkg
    cp /root/zjnodes/zjchain/conf/GeoLite2-City.mmdb /root/zjnodes/zjchain/pkg
    cp /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/zjchain/pkg
    cp /root/shardora/shards3 /root/zjnodes/zjchain/pkg
    cp /root/shardora/root_nodes /root/zjnodes/zjchain/pkg/shards2
    cp /root/shardora/temp_cmd.sh /root/zjnodes/zjchain/pkg
    cp /root/shardora/start_cmd.sh /root/zjnodes/zjchain/pkg
    cp /root/shardora/wondershaper /root/zjnodes/zjchain/pkg
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/zjchain/pkg/shard_db_2
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/zjchain/pkg
    cp -rf /root/zjnodes/temp /root/zjnodes/zjchain/pkg
    cp -rf /root/shardora/gdb/* /root/zjnodes/zjchain/pkg
    cd /root/zjnodes/zjchain/ && tar -zcvf pkg.tar.gz ./pkg > /dev/null 2>&1
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
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && rm -rf pkg* ; rm -rf zjnodes ; killall -9 zjchain" &
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 10)); then
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
        sshpass -p $PASSWORD scp -o ConnectTimeout=10  -o StrictHostKeyChecking=no /root/zjnodes/zjchain/pkg.tar.gz root@$ip:/root &
        run_cmd_count=$((run_cmd_count + 1))
        if (($run_cmd_count >= 10)); then
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

        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && tar -zxvf pkg.tar.gz && cd ./pkg && sh temp_cmd.sh $ip $start_pos $start_nodes_count $bootstrap 2 $end_shard"  > /dev/null 2>&1 &
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

        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root/pkg && sh start_cmd.sh $ip $start_pos $start_nodes_count $bootstrap 2 $end_shard "  &
        if ((start_pos==1)); then
            sleep 3
        fi

        sleep 0.3
        start_pos=$(($start_pos+$start_nodes_count))
    done

    check_cmd_finished
    echo 'start_all_nodes over'
}

killall -9 sshpass
init 
# make_package
clear_command
# scp_package
# get_bootstrap
# echo $bootstrap
# run_command
# start_all_nodes
