#!/bin/bash

net_id=$1

last_line=$(grep "total tps" /root/zjnodes/s${net_id}_2/log/shardora.log | tail -n 1)
tps_value=$(echo "$last_line" | grep -oP 'tps: \K[0-9]+\.[0-9]+')

echo $tps_value

