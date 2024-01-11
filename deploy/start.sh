#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
# ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

cp /root/deploy/zjchain@.service /etc/systemd/system/zjchain@.service
sudo systemctl daemon-reload

sh stop.sh

services=("r1" "r2" "r3" "s1" "s2" "s3" "s4" "s5" "s6" "s7" "s8" "s9" "s10" "s11")

for service in "${services[@]}"; do
  sudo systemctl start "zjchain@${service}.service"
  
  # 等待 5 秒，除了最后一次启动
  if [[ "${service}" == "${services[0]}" ]]; then
    sleep 5
  fi
done

sleep 3
