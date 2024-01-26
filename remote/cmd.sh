IP=(\
10.101.20.12 \
10.101.20.29 \
10.101.20.30 \
10.101.20.31 \
10.101.20.32 \
10.101.20.33 \
10.101.20.34 \
10.101.20.35 \
10.101.20.36 \
)

for ip in ${IP[@]};
do
    echo $ip
    sshpass -p 123456 ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5  root@$ip $1 & 
    #sleep 3
done
