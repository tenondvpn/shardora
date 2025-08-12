node_ips=$1
node_ips_array=(${node_ips//,/ })
for ip in "${node_ips_array[@]}"; do
    echo $ip
    sshpass -p Xf4aGbTaf! scp -r -o "StrictHostKeyChecking no" /root/shardora/pkg.tar.gz root@$ip:/root
    sshpass -p Xf4aGbTaf! scp -r -o "StrictHostKeyChecking no" /root/shardora/cp_pkg.sh root@$ip:/root
done

