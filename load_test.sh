#!/bin/bash

if [[ $1 == "stop" ]];
then
ps -ef | grep txcli | awk -F' ' '{print $2}' | xargs kill -9	
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
ps -ef | grep txcli | awk -F' ' '{print $2}' | xargs kill -9
EOF

exit
fi

cd /root/xufei/shardora && make txcli && cd cbuild_Release

pass="Xf4aGbTaf!"

sshpass -p $pass scp -o StrictHostKeyChecking=no -r ./txcli ../addrs root@192.168.0.3:/root/

sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
cd /root && ./txcli 0 3 15 192.168.0.3 13003 20 &
cd /root && ./txcli 0 4 15 192.168.0.3 14003 20 &
wait \
'"

./txcli 0 3 9 192.168.0.2 13002 20 &
./txcli 0 4 12 192.168.0.2 14002 20 &
wait
