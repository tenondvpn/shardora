#!/bin/bash

# sh -x fetch.sh r1 r2 r3 s1 s2 s3

rm -rf /root/zjnodes
rm -rf /root/deploy

scp -r root@10.101.20.35:/root/zjnodes /root/zjnodes
scp -r root@10.101.20.35:/root/deploy /root/deploy

# 获取本机 IP 地址
newip=$(ifconfig | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v '127.0.0.1')

services=("${@}")

for service in "${services[@]}"; do
	# 除了 bootstrap 那一行其余都执行替换
	sed -i "/bootstrap/!s/10.101.20.35/${newip}/g" "zjnodes/${service}/conf/zjchain.conf"
done
