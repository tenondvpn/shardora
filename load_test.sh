#!/bin/bash


/root/xufei/shardora/cbuild_Release/txcli 0 3 15 192.168.0.2 13002 20 &
/root/xufei/shardora/cbuild_Release/txcli 0 3 9 192.168.0.2 13002 20 &
/root/xufei/shardora/cbuild_Release/txcli 0 4 14 192.168.0.2 14002 20 &
/root/xufei/shardora/cbuild_Release/txcli 0 4 15 192.168.0.2 14002 20 &
wait
