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
    sshpass -p 123456 scp -o "StrictHostKeyChecking no" $1 root@$ip:$2 
    echo $ip
done
