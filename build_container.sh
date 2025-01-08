#!/bin/bash

# 检查是否传入了 net_id 参数
if [ -z "$1" ]; then
  echo "Error: Missing shard_db_name argument. Please specify the database name (e.g., root, shard_3)."
  exit 1
fi

net_id=$1 # eg. 2, 3, 4
target=$2
no_build=$3

if [ "$net_id" -eq 2 ]; then
    shard_db_name="root_db"
	net_name="root"
else
	shard_db_name="shard_${net_id}_db"
	net_name="shard${net_id}"
fi

sh gen_docker_files.sh $target $no_build

target_lower=$(echo $target | tr '[:upper:]' '[:lower:]')
image_name="shardora-image-${net_name}-${target_lower}"

echo "Building image ${image_name}"

docker build --build-arg SHARD_DB=$shard_db_name -t ${image_name}:1.0 ./docker_files

# 检查构建是否成功
if [ $? -eq 0 ]; then
  echo "Docker image ${image_name}:1.0 built successfully."
else
  echo "Error: Failed to build Docker image."
  exit 1
fi
