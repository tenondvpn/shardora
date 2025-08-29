#!/bin/bash

target=$1
no_build=$2

sh gen_docker_files.sh $target $no_build

target_lower=$(echo $target | tr '[:upper:]' '[:lower:]')
image_name="shardora-image-${target_lower}"

echo "Building image ${image_name}"

docker build -t ${image_name} ./docker_files

# 检查构建是否成功
if [ $? -eq 0 ]; then
  echo "Docker image ${image_name} built successfully."
else
  echo "Error: Failed to build Docker image."
  exit 1
fi
