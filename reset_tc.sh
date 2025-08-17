node_ips=$1

reset_tc() {
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

    DEV=eth0
    RATE="1000mbit"
    DELAY="50ms 10ms loss 0.01%"
    #DELAY="25ms"

    sh cmd.sh $node_ips "tc qdisc del dev $DEV root && tc qdisc add dev $DEV root handle 1: htb default 12 && tc class add dev $DEV parent 1: classid 1:1 htb rate 1000mbit && tc class add dev $DEV parent 1:1 classid 1:12 htb rate $RATE ceil $RATE && tc qdisc add dev $DEV parent 1:12 handle 12: netem delay $DELAY "  > /dev/null 2>&1 &
}


killall -9 sshpass
reset_tc 
