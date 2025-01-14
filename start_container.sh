#!/bin/bash

# 配置文件路径
image_name=$1
config_file=$2
container_name=$3

if [[ ! -f "$config_file" ]]; then
    echo "Error: Configuration file '$config_file' does not exist."
    exit 1
fi

# 提取 local_port 和 http_port
local_ip=$(grep -E "^local_ip" "$config_file" | awk -F '=' '{print $2}' | xargs)
local_port=$(grep -E "^local_port" "$config_file" | awk -F '=' '{print $2}' | xargs)
http_port=$(grep -E "^http_port" "$config_file" | awk -F '=' '{print $2}' | xargs)


# 输出提取的 IP 和端口，确保正确
echo "LOCAL IP: $local_ip"
echo "LOCAL Port: $local_port"
echo "HTTP Port: $http_port"

# 运行 Docker 容器，并将 IP 和端口号通过环境变量传递给容器
# docker run -d \
#   -v ${config_file}:/root/node/conf/zjchain.conf \
#   -e LOCAL_IP=${local_ip} \
#   -e LOCAL_PORT=${local_port} \
#   -e HTTP_PORT=${http_port} \
#   -p ${http_port}:${http_port} \
#   -p ${local_port}:${local_port} \
#   ${image_name}
# 替换为你的 Docker 镜像名称

  # 替换为你的 Docker 镜像名称

if [ -n $container_name ]
then
    docker run --network host -d \
		   --name ${container_name} \
		   -v ${config_file}:/root/node/conf/zjchain.conf \
		   ${image_name}	
else
    docker run --network host -d \
		   -v ${config_file}:/root/node/conf/zjchain.conf \
		   ${image_name}
fi
