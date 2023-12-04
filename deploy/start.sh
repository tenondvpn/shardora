#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
# ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

cp /root/deploy/zjchain@.service /etc/systemd/system/zjchain@.service
sudo systemctl daemon-reload

sh stop.sh

sudo systemctl start zjchain@r1.service
sleep 5

sudo systemctl start zjchain@r2.service
sudo systemctl start zjchain@r3.service
sudo systemctl start zjchain@s1.service
sudo systemctl start zjchain@s2.service
sudo systemctl start zjchain@s3.service

sleep 3

