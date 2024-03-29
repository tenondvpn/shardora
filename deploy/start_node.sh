#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

if test "$1" = "r1"
then
	cd /root/zjnodes/$1/ && nohup ./zjchain -f 1 -g 0 "$1" & # $1 只是为了标识节点名字，没有用
else
	cd /root/zjnodes/$1/ && nohup ./zjchain -f 0 -g 0 "$1" &
fi
