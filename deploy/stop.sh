#!/bin/bash

# ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
services=("$@")

for service in "${services[@]}"; do
  sudo systemctl stop "zjchain@${service}.service"
done
