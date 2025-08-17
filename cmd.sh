node_ips=$1
node_ips_array=(${node_ips//,/ })
for ip in "${node_ips_array[@]}"; do
    echo $ip
    sshpass -p Xf4aGbTaf! ssh -o ConnectTimeout=10 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip $2  
    #sleep 3
done
