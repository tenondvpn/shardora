#!/bin/bash

cd /root/xufei/shardora/cbuild_Release

# ./txcli 0 $net_id $pool_id $ip $port $delay_us

./txcli 0 3 15 192.168.0.2 13002 20 &
./txcli 0 3 9 192.168.0.2 13002 20 &
./txcli 0 4 14 192.168.0.2 14002 20 &
./txcli 0 4 15 192.168.0.2 14002 20 &
wait
