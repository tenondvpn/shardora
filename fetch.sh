#!/bin/bash

# sh -x fetch.sh 10.101.20.36 r1 r2 r3 s1 s2 s3

rm -rf /root/zjnodes
rm -rf /root/deploy

scp -r root@10.101.20.35:/root/zjnodes /root/zjnodes
scp -r root@10.101.20.35:/root/deploy /root/deploy

newip="$1"
services=("${@:2}")

for service in "${services[@]}"; do
	sed -i "s/10.101.20.35/${newip}/g" "zjnodes/${service}/conf/zjchain.conf"
done
