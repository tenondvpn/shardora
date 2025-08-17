node_ips=$1
cmd=$2
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
    for ip in "${node_ips_array[@]}"; do 
        sshpass -p Xf4aGbTaf! ssh -o ConnectTimeout=10 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip "$cmd" &
        run_cmd_count=$(($run_cmd_count + 1))
        if (($run_cmd_count >= 50)); then
            check_cmd_finished
            run_cmd_count=0
        fi
    done

    check_cmd_finished
    echo 'run_command over'
}

clear_command
