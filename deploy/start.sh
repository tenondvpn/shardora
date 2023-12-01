#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
# ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

sudo systemctl start zjchain@r1.service &
sleep 3

sudo systemctl start zjchain@r2.service &
sudo systemctl start zjchain@r3.service &
sudo systemctl start zjchain@s1.service &
sudo systemctl start zjchain@s2.service &
sudo systemctl start zjchain@s3.service &


exit 0
