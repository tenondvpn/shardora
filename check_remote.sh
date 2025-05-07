each_nodes_count=$1
node_ips=$2
PASSWORD=$3
RUNC_COMMAND_STR=$4

init() {
    if [ "$node_ips" == "" ]; then
        echo "just use local single node."
        node_ips='127.0.0.1'
    fi  

    if [ "$PASSWORD" == "" ]; then
        PASSWORD="Xf4aGbTaf!"
    fi

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
    fi  

    echo "node count: " $nodes_count
}

run_command() {
    echo 'run_command start'
    node_ips_array=(${node_ips//,/ })
    for ip in "${node_ips_array[@]}"; do 
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "$RUNC_COMMAND_STR"
    done

    echo 'run_command over'
}

init

if [ "$RUNC_COMMAND_STR" == "check_network" ]; then
    node_ips_array=(${node_ips//,/ })
    for ip in "${node_ips_array[@]}"; do 
        ip_node_count=`sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "ps -ef | grep zjchain" | grep s3 | wc -l`
        if [ $ip_node_count -eq $each_nodes_count ]; then
            echo $ip " valid node count: " $ip_node_count
        else
            echo $ip " ERROR node count: " $ip_node_count " valid node count: " $ip_node_count
        fi
    done
fi

