node_ips=$1
node_ips_array=(${node_ips//,/ })
for ip in "${node_ips_array[@]}"; do
    echo $ip
    #sshpass -p Xf4aGbTaf9 scp -o "StrictHostKeyChecking no" $1 root@$ip:$2 
    sshpass -p Xf4aGbTaf! scp -r -o "StrictHostKeyChecking no" root@$ip:$2 $3/$ip
done
