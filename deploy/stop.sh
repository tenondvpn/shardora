#!/bin/bash

# ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
services=("r1" "r2" "r3" "s1" "s2" "s3" "s4" "s5" "s6" "s7" "s8" "s9" "s10" "s11")

for service in "${services[@]}"; do
  sudo systemctl stop "zjchain@${service}.service"
done
