#!/bin/bash

pass='Xf4aGbTaf!'

if [[ $1 == "stop" ]];
then
ps -ef | grep txcli | awk -F' ' '{print $2}' | xargs kill -9	
sshpass -p $pass ssh -o StrictHostKeyChecking=no root@192.168.0.3 <<"EOF"
ps -ef | grep txcli | awk -F' ' '{print $2}' | xargs kill -9
EOF

exit
fi

cd /root/xufei/shardora/cbuild_Release && make txcli



sshpass -p $pass ssh -f -o StrictHostKeyChecking=no root@192.168.0.3 bash -c "'\
mkdir /root/load_test
wait \
'"

sshpass -p $pass scp -o StrictHostKeyChecking=no ./txcli root@192.168.0.3:/root/load_test
sshpass -p $pass scp -o StrictHostKeyChecking=no ../addrs root@192.168.0.3:/root/

sshpass -p $pass ssh -f -o StrictHostKeyChecking=no root@192.168.0.3 bash -c "'\
cd /root/load_test && ./txcli 0 3 15 192.168.0.3 13003 20 &
cd /root/load_test && ./txcli 0 4 15 192.168.0.3 14003 20 &
wait \
'"

./txcli 0 3 9 192.168.0.2 13002 20 &
./txcli 0 4 12 192.168.0.2 14002 20 &
