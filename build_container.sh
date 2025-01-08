#!/bin/bash

# 检查是否传入了 shard_db_name 参数
if [ -z "$2" ]; then
  echo "Error: Missing shard_db_name argument. Please specify the database name (e.g., root_db, shard_3_db)."
  exit 1
fi

TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

sh gen_docker_files.sh $TARGET

shard_db_name=$2 # eg. root_db shard_3_db

docker build --build-arg SHARD_DB=$shard_db_name -t shardora-image-$TARGET-$shard_db_name:1.0 ./docker_files

# 检查构建是否成功
if [ $? -eq 0 ]; then
  echo "Docker image shardora-image-$shard_db_name:1.0 built successfully."
else
  echo "Error: Failed to build Docker image."
  exit 1
fi
