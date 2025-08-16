node_ips=$1
node_ips_array=(${node_ips//,/ })
for ip in "${node_ips_array[@]}"; do
    sshpass -p Xf4aGbTaf! scp -r -o "StrictHostKeyChecking no" /root/pkg.tar.gz root@$ip:/root
done