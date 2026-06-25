each_nodes_count=$1
node_ips=$2
bootstrap=""
end_shard=$3
PASSWORD=$4
TARGET=$5
FIRST_NODE_COUNT=$1

# bash cmd.sh $2 "systemctl list-units --state=active --no-legend | grep shardora@ | awk '{print \$1}' | xargs -r systemctl stop; killall -9 shardora"
init() {
    tmp_ips=(${node_ips//-/ })
    tmp_ips_len=(${#tmp_ips[*]})
    ip_max_idx=0
    if (($tmp_ips_len > 1)); then
        for tmp_ip_nodes in "${tmp_ips[@]}"; do
            ips_array=(${tmp_ip_nodes//,/ })
            first_ip=(${ips_array[0]})
            second_ip=(${ips_array[1]})

            start=$(($first_ip + 0))
            end=$(($second_ip + 0))
            for ((i=start; i<=end; i++)); do
                if ((i==end));then
                    new_ips+="192.168.$ip_max_idx.$i"
                else
                    new_ips+="192.168.$ip_max_idx.$i,"
                fi
            done

            new_ips+=","
            ip_max_idx=$(($ip_max_idx+1))
        done

        node_ips=$new_ips
        echo $node_ips
    else
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
    fi

    if [ "$node_ips" == "" ]; then
        echo "just use local single node."
        node_ips='127.0.0.1'
    fi

    bash cmd.sh $node_ips "tc qdisc del dev eth0 root"  > /dev/null 2>&1 &
    if [ "$end_shard" == "" ]; then
        end_shard=3
    fi

    if [ "$PASSWORD" == "" ]; then
        PASSWORD="Xf4aGbTaf&"
    fi

    if [ "$TARGET" == "" ]; then
        TARGET=Debug
    fi


    bash build.sh a $TARGET
    cd /root/shardora/cbuild_$TARGET && make txcli
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
        sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "cd /root && rm -rf /root/pkg/shardora /root/pkg/txcli" &
        run_cmd_count=$((run_cmd_count + 1))
        if ((start_pos==1)); then
            sleep 3
        fi

        if (($run_cmd_count >= 250)); then
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
        sshpass -p $PASSWORD scp -o ConnectTimeout=10  -o StrictHostKeyChecking=no /root/shardora/cbuild_$TARGET/shardora root@$ip:/root/pkg/ &
        run_cmd_count=$((run_cmd_count + 1))
        if (($run_cmd_count >= 100)); then
            check_cmd_finished
            run_cmd_count=0
        fi
    done

    check_cmd_finished
    echo 'scp_package over'
}

ln_all_nodes() {
    echo 'start_all_nodes start'
    node_ips_array=(${node_ips//,/ })
    for ((shard=2; shard<=end_shard; shard++)); do
        start_pos=1
        for ip in "${node_ips_array[@]}"; do
            echo "start node: " $ip $each_nodes_count
            start_nodes_count=$(($each_nodes_count + 0))
            if ((start_pos==1)); then
                start_nodes_count=$FIRST_NODE_COUNT
            fi

            for ((i=1; i<=$each_nodes_count;i++)); do
                echo "/root/shardoras/s${shard}_${start_pos}/"
                REMOTE_CMD="if [ -d \"/root/shardoras/s${shard}_${start_pos}/\" ]; then 
                                rm -rf /root/shardoras/s${shard}_${start_pos}/shardora && \
                                rm -rf /root/shardoras/s${shard}_${start_pos}/log/* && \
                                ln /root/pkg/shardora /root/shardoras/s${shard}_${start_pos}/shardora && \
                                echo \"[Shard $shard] Updated\"; 
                            else 
                                echo \"[Shard $shard] Not exists, skipping\"; 
                            fi"
                sshpass -p "$PASSWORD" ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip "$REMOTE_CMD" &
                if ((start_pos == 1)); then
                    sleep 3
                fi
                start_pos=$(($start_pos+1))
            done
        done
        sleep 0.1
    done

    check_cmd_finished
    echo 'start_all_nodes over'
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

        REMOTE_CMD="pkill -9 shardora "
        sshpass -p "$PASSWORD" ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip "$REMOTE_CMD" &
        if ((start_pos == 1)); then
            sleep 3
        fi
        sleep 0.1
        start_pos=$(($start_pos+$start_nodes_count))
    done

    check_cmd_finished
    echo 'start_all_nodes over'
}

killall -9 sshpass
init
clear_command
scp_package
ln_all_nodes
start_all_nodes
