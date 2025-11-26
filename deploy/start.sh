#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
# ps -ef | grep shardora | awk -F' ' '{print $2}' | xargs kill -9

cp /root/deploy/shardora@.service /etc/systemd/system/shardora@.service
sudo systemctl daemon-reload

services=("$@")

sh stop.sh "$@"


for service in "${services[@]}"; do
  sudo systemctl start "shardora@${service}.service"
  
  # # 等待 5 秒，除了最后一次启动
  # if [[ "${service}" == "${services[0]}" ]]; then
  #   sleep 5
  # fi
done

sleep 3

