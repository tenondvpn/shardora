#!/bin/bash

# 提取 local_port 和 http_port
net_id=$(grep -E "^net_id" "./conf/zjchain.conf" | awk -F '=' '{print $2}' | xargs)

if [ "$net_id" -eq 2 ]; then
    shard_db_name="root_db"
else
	shard_db_name="shard_db_${net_id}"
fi

echo "shard_db_name: $shard_db_name"

mv ./$shard_db_name ./db

./zjchain -f 0 -g 0
