#!/bin/bash

# 检查是否传入了 shard_db_name 参数
if [ -z "$1" ]; then
  echo "Error: Missing shard_db_name argument. Please specify the database name (e.g., root_db, shard_3_db)."
  exit 1
fi

shard_name=$1 # eg. root shard_3
target=$2
no_build=$3
shard_db_name="${shard_name}_db"

sh gen_docker_files.sh $target $no_build

target_lower=echo $target | tr '[:upper:]' '[:lower:]'
image_name="shardora-image-${target_lower}-${shard_name}"

docker build --build-arg SHARD_DB=$shard_db_name -t ${image_name}:1.0 ./docker_files

# 检查构建是否成功
if [ $? -eq 0 ]; then
  echo "Docker image ${image_name}:1.0 built successfully."
else
  echo "Error: Failed to build Docker image."
  exit 1
fi
