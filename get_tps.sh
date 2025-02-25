#!/bin/bash

net_id=$1

last_line=$(grep "HandleTimerMessage" /root/zjnodes/s${net_id}_2/log/zjchain.log | tail -n 1)
tps_value=$(echo "$last_line" | grep -oP 'tps: \K[0-9]+\.[0-9]+')

echo $tps_value

