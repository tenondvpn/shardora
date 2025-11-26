#!/bin/bash

for node in r1; do
sh start_container.sh shardora-image-debug ./zjnodes/$node/conf/shardora.conf $node 
done

sleep 3

for node in r2 r3 s3_1 s3_2 s3_3 s3_4; do
sh start_container.sh shardora-image-debug ./zjnodes/$node/conf/shardora.conf $node
done
