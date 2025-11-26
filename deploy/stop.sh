#!/bin/bash

# ps -ef | grep shardora | awk -F' ' '{print $2}' | xargs kill -9
services=("$@")

for service in "${services[@]}"; do
  sudo systemctl stop "shardora@${service}.service"
done
