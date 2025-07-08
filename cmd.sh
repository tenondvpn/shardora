IP=(\
192.168.0.187 \
192.168.0.10 \
192.168.0.117 \
192.168.0.76 \
)


for ip in ${IP[@]};
do
    echo $ip
    sshpass -p Xf4aGbTaf! ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip $1  
    #sleep 3
done

