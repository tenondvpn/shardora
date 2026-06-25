each_nodes_count=$1
node_ips=$2
bootstrap=""
end_shard=$3
PASSWORD=$4
TARGET=$5

if [ "$end_shard" == "" ]; then
    end_shard=3
fi

CODE_PATH=`pwd`
node_hash=$(printf "%s%d" "$node_ips" "$each_nodes_count" "$end_shard" | md5sum | cut -d ' ' -f1)
declare -A shard_map

bash cmd.sh $2 "systemctl list-units --state=active --no-legend | grep shardora@ | awk '{print \$1}' | xargs -r systemctl stop; killall -9 shardora"
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
   
    if [ "$PASSWORD" == "" ]; then
        PASSWORD="Xf4aGbTaf&"
    fi

    if [ "$TARGET" == "" ]; then
        TARGET=Debug
    fi

    killall -9 shardora
    killall -9 txcli

    bash build.sh a $TARGET
    sudo rm -rf /root/nodes
    sudo cp -rf ./nodes_local /root/nodes
    rm -rf /root/nodes/*/shardora /root/nodes/*/core* /root/nodes/*/log/* /root/nodes/*/*db*

    cp -rf ./nodes_local/shardora/conf/GeoLite2-City.mmdb /root/nodes/shardora
    cp -rf ./nodes_local/shardora/conf/log4cpp.properties /root/nodes/shardora/conf
    mkdir -p /root/nodes/shardora/log


    sudo cp -rf ./cbuild_$TARGET/shardora /root/nodes/shardora
    if [[ "$each_nodes_count" -eq "" ]]; then
        each_nodes_count=4
    fi

    if [ "$end_shard" == "" ]; then
        end_shard=3
    fi

    start_shard=2
    total_shards=$((end_shard - start_shard + 1))
    node_ips_array=(${node_ips//,/ })
    total_ips=${#node_ips_array[@]}

   
    for ((i=0; i<$total_ips; i++)); do
        shard_idx=$((i % total_shards))
        current_shard=$((shard_idx + start_shard))
        shard_map[$current_shard]+="${node_ips_array[$i]} "
    done

    nodes_count=$(((total_ips / total_shards) * 10))

    echo "Node count: $nodes_count"
    echo "Original IPs: $node_ips"
    echo "Shard Mapping Details:"
    for shard in "${!shard_map[@]}"; do
        echo "  [Shard $shard]: ${shard_map[$shard]}"
    done

    rm -rf /root/nodes/shardora/latest_blocks
}

make_package() {
    mkdir -p /root/shardora/pkgs
    rm -rf /root/nodes/shardora/pkg
    if [ -d "/root/shardora/pkgs/$node_hash" ]; then
        cd /root/shardora/ && bash build.sh a $TARGET
        cd /root/shardora/cbuild_$TARGET && make txcli
        cp -rf /root/shardora/cbuild_$TARGET/shardora /root/shardora/pkgs/$node_hash/shardora
        cp -rf /root/shardora/pkgs/$node_hash /root/nodes/shardora/pkg
        rm -rf /root/nodes/shardora/pkg/temp
        cp -rf /root/nodes/temp /root/nodes/shardora/pkg
        cp /root/shardora/temp_cmd.sh /root/nodes/shardora/pkg
        cp /root/shardora/start_cmd.sh /root/nodes/shardora/pkg
        for ((shard_id=2; shard_id<=$end_shard; shard_id++)); do
            /root/shardora/cbuild_$TARGET/shardora -A /root/shardora/shards${shard_id} -D /root/nodes/shardora/pkg/shards${shard_id}
            /root/shardora/cbuild_$TARGET/shardora -A  /root/shardora/init_accounts${shard_id} -D /root/nodes/shardora/pkg/init_accounts${shard_id}
        done
    else
        end_shard_index=$((end_shard + 1))
        echo "./shardora -U -N ${nodes_count} -E ${end_shard_index}"
        echo "./shardora -S 3 -N ${nodes_count} -E ${end_shard_index}"
        cd /root/nodes/shardora && ./shardora -U -N ${nodes_count} -E ${end_shard_index}
        cd /root/nodes/shardora && ./shardora -S 3 -N ${nodes_count} -E ${end_shard_index}
        cd /root/nodes/shardora && ./shardora -C
        cd /root/shardora/cbuild_$TARGET && make txcli

        mkdir /root/nodes/shardora/pkg
        cp /root/nodes/shardora/shardora /root/nodes/shardora/pkg
        cp /root/nodes/shardora/conf/GeoLite2-City.mmdb /root/nodes/shardora/pkg
        cp /root/nodes/shardora/conf/log4cpp.properties /root/nodes/shardora/pkg
        for ((shard_id=2; shard_id<=$end_shard; shard_id++)); do
            /root/shardora/cbuild_$TARGET/shardora -A /root/shardora/shards${shard_id} -D /root/nodes/shardora/pkg/shards${shard_id}
            /root/shardora/cbuild_$TARGET/shardora -A  /root/shardora/init_accounts${shard_id} -D /root/nodes/shardora/pkg/init_accounts${shard_id}
        done
        cp /root/shardora/temp_cmd.sh /root/nodes/shardora/pkg
        cp /root/shardora/start_cmd.sh /root/nodes/shardora/pkg
        cp -rf /root/nodes/shardora/shard_db* /root/nodes/shardora/pkg/
        cp -rf /root/nodes/temp /root/nodes/shardora/pkg
        cp -rf /root/shardora/gdb/* /root/nodes/shardora/pkg
        cp -rf /root/nodes/shardora/pkg /root/shardora/pkgs/$node_hash
    fi

    cd /root/nodes/shardora/ && tar -zcvf pkg.tar.gz ./pkg > /dev/null 2>&1
}

get_bootstrap() {
    node_ips_array=(${node_ips//,/ })
    for ((shard_id=2; shard_id<=$end_shard; shard_id++)); do
        i=1
        for ip in "${node_ips_array[@]}"; do
            for ((j=0; j<$each_nodes_count; j++)); do
                tmppubkey=`sed -n "$i""p" /root/nodes/shardora/pkg/shards${shard_id} | awk -F'\t' '{print $2}'`
                port=''
                if ((i>=100)); then
                    port='1'$shard_id''$i
                elif ((i>=10)); then
                    port='1'$shard_id'0'$i
                else
                    port='1'$shard_id'00'$i
                fi

                if (( port > 65535 )); then
                    (( port = (port % 60000) + 1024 ))
                fi

                node_info=$tmppubkey":"$ip":"$port":"$shard_id
                bootstrap=$bootstrap","$node_info
                i=$((i+1))
            done
        done
    done

    # Write bootstrap into the conf template via Python (handles long strings safely)
    printf "%s" "$bootstrap" > /tmp/bootstrap_data.tmp
    /root/tools/python3.10/bin/python3 -c "
conf_path = '/root/nodes/shardora/pkg/temp/conf/shardora.conf'
with open('/tmp/bootstrap_data.tmp', 'r') as f:
    new_val = f.read()
with open(conf_path, 'r') as f:
    content = f.read()
with open(conf_path, 'w') as f:
    f.write(content.replace('BOOTSTRAP', new_val))
"
    rm /tmp/bootstrap_data.tmp
    echo $bootstrap

    # Re-pack after BOOTSTRAP has been written into the conf
    cd /root/nodes/shardora/ && tar -zcvf pkg.tar.gz ./pkg > /dev/null 2>&1
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
    for ((shard_id=start_shard; shard_id<=$end_shard; shard_id++)); do
        ips=(${shard_map[$shard_id]})
        for ip in "${ips[@]}"; do
            sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip  "cd /root && rm -rf pkg*; killall -9 shardora" &
            run_cmd_count=$((run_cmd_count + 1))
            if (($run_cmd_count >= 250)); then
                check_cmd_finished
                run_cmd_count=0
            fi
        done
    done

    check_cmd_finished
    echo 'run_command over'
}

scp_package() {
    echo 'scp_package start'
    run_cmd_count=0
    for ((shard_id=start_shard; shard_id<=$end_shard; shard_id++)); do
        ips=(${shard_map[$shard_id]})
        echo 'run_cstart_ascp_packagell_nodesommand: ' $shard_id $ips
        for ip in "${ips[@]}"; do
            echo "scp_package: " $ip
            sshpass -p $PASSWORD scp -o ConnectTimeout=10  -o StrictHostKeyChecking=no /root/nodes/shardora/pkg.tar.gz root@$ip:/root &
            run_cmd_count=$((run_cmd_count + 1))
            if (($run_cmd_count >= 100)); then
                check_cmd_finished
                run_cmd_count=0
            fi
        done
    done

    check_cmd_finished
    echo 'scp_package over'
}

run_command() {
    echo 'run_command start'
    run_cmd_count=0
    for ((shard_id=start_shard; shard_id<=$end_shard; shard_id++)); do
        start_pos=1
        ips=(${shard_map[$shard_id]})
        echo 'run_command: ' $shard_id $ips
        for ip in "${ips[@]}"; do
            echo "config node: " $ip $each_nodes_count
            start_nodes_count=$(($each_nodes_count + 0))
            leader_init_tm=$(date -u -d "+240 seconds" +%s)
            echo 'sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip "cd /root && tar -zxvf pkg.tar.gz && cd ./pkg && bash temp_cmd.sh $ip $start_pos $start_nodes_count $bootstrap $shard_id $shard_id $leader_init_tm"'
            sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip "cd /root && tar -zxvf pkg.tar.gz && cd ./pkg && bash temp_cmd.sh $ip $start_pos $start_nodes_count $bootstrap $shard_id $shard_id $leader_init_tm"  > /dev/null 2>&1 &
            run_cmd_count=$(($run_cmd_count + 1))
            if (($run_cmd_count >= 250)); then
                check_cmd_finished
                run_cmd_count=0
            fi
            start_pos=$(($start_pos+$start_nodes_count))
        done
    done

    check_cmd_finished
    echo 'run_command over'
}

start_all_nodes() {
    echo 'start_all_nodes start'
    for ((shard_id=start_shard; shard_id<=$end_shard; shard_id++)); do
        start_pos=1
        ips=(${shard_map[$shard_id]})
        echo 'run_cstart_all_nodesommand: ' $shard_id $ips
        for ip in "${ips[@]}"; do
            echo "start node: " $ip $each_nodes_count
            start_nodes_count=$(($each_nodes_count + 0))
            echo 'sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip "cd /root/pkg && bash start_cmd.sh $ip $start_pos $start_nodes_count $bootstrap $shard_id $shard_id "'
            sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$ip "cd /root/pkg && bash start_cmd.sh $ip $start_pos $start_nodes_count $bootstrap $shard_id $shard_id "  &
            if ((start_pos==1)); then
                sleep 3
            fi

            sleep 0.1
            start_pos=$(($start_pos+$start_nodes_count))
        done
    done
    
    check_cmd_finished
    echo 'start_all_nodes over'
}

killall -9 sshpass
init
make_package
clear_command
get_bootstrap
echo $bootstrap
scp_package
run_command
start_all_nodes