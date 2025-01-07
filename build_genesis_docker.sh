#!/bin/bash

target=$1
no_build=$2

sh ./build_genesis.sh $target $no_build

# 删除并创建 docker_files 目录
rm -rf ./docker_files && mkdir ./docker_files

mkdir -p ./docker_files/node
mkdir -p ./docker_files/node/log
mkdir -p ./docker_files/node/conf

# 复制文件到 docker_files 目录
cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb ./docker_files/node/conf
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties ./docker_files/node/conf
cp -rf ./cbuild_$target/zjchain ./docker_files/node
cp -f ./conf/genesis.yml ./docker_files/node/genesis.yml

# 复制 _db 文件到 docker_files
cp -rf /root/zjnodes/zjchain/*_db ./docker_files/node/

cp ./Dockerfile ./docker_files
