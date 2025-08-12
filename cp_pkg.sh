for ((i=2; i<=$1; i++))
do
    ip='192.168.0.'$i
    echo $ip
    sshpass -p Xf4aGbTaf! scp -r -o "StrictHostKeyChecking no" /root/pkg.tar.gz root@$ip:/root
done
