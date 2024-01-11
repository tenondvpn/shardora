#!/bin/bash

# sh -x fetch.sh 10.101.20.35 10.101.20.36 r1 r2 r3 s1 s2 s3

fromip="$1"
newip="$2"
services=("${@:3}")

if [[ "${fromip}" == "127.0.0.1" ]]; then
	for service in "${services[@]}"; do
		# 除了 bootstrap 那一行其余都执行替换
		sed -i "s/${fromip}/${newip}/g" "zjnodes/${service}/conf/zjchain.conf"
	done
else
	rm -rf /root/zjnodes
	rm -rf /root/deploy
	scp -r root@"${fromip}":/root/zjnodes /root/zjnodes
	scp -r root@"${fromip}":/root/deploy /root/deploy
	
	for service in "${services[@]}"; do
		# 除了 bootstrap 那一行其余都执行替换
		sed -i "/bootstrap/!s/${fromip}/${newip}/g" "zjnodes/${service}/conf/zjchain.conf"
	done
fi
