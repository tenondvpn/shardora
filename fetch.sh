#!/bin/bash

# sh -x fetch.sh 10.101.20.35 10.101.20.36 r1 r2 r3 s1 s2 s3

fromip="$1"
newip="$2"
pass="$3"
datadir="$4"
services=("${@:5}")


if [[ -z $1 ]]; then
    echo "Error: SrcIp is required."
    exit 1
fi

if [[ -z $2 ]]; then
    echo "Error: LocalIp is required."
    exit 1
fi

if [[ "${fromip}" == "127.0.0.1" ]]; then
	for service in "${services[@]}"; do
		# 除了 bootstrap 那一行其余都执行替换
		sed -i "s/${fromip}/${newip}/g" "${datadir}/zjnodes/${service}/conf/zjchain.conf"
	done
else
	rm -rf "${datadir}"/zjnodes
	rm -rf "${datadir}"/deploy
	sshpass -p $pass scp -o StrictHostKeyChecking=no -r root@"${fromip}":"${datadir}"/zjnodes "${datadir}"/zjnodes
	sshpass -p $pass scp -o StrictHostKeyChecking=no -r root@"${fromip}":"$datadir"/deploy "${datadir}"/deploy
	
	for service in "${services[@]}"; do
		# 除了 bootstrap 那一行其余都执行替换
		sed -i "/bootstrap/!s/${fromip}/${newip}/g" "${datadir}/zjnodes/${service}/conf/zjchain.conf"
	done
fi
