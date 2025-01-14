#!/bin/bash

for node in r1; do
sh start_container.sh shardora-image-root-debug ./zjnodes/$node/conf/zjchain.conf 
done

sleep 3

for node in r2 r3; do
sh start_container.sh shardora-image-root-debug ./zjnodes/$node/conf/zjchain.conf 
done

for node in s3_1 s3_2 s3_3 s3_4; do
sh start_container.sh shardora-image-shard3-debug ./zjnodes/$node/conf/zjchain.conf	
done
