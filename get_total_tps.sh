#!/bin/bash

# 获取日志中的 tps 数值
last_line=$(grep "total tps" /root/zjnodes/s3_2/log/shardora.log | tail -n 1)
tps3=$(echo "$last_line" | grep -oP 'tps: \K[0-9]+\.[0-9]+')

last_line=$(grep "total tps" /root/zjnodes/s4_2/log/shardora.log | tail -n 1)
tps4=$(echo "$last_line" | grep -oP 'tps: \K[0-9]+\.[0-9]+')

# 使用 bc 进行浮动数值加法
total_tps=$(echo "$tps3 + $tps4" | bc)

echo "$total_tps"
