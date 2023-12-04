#!/bin/bash

# ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

sudo systemctl stop zjchain@r1.service
sudo systemctl stop zjchain@r2.service
sudo systemctl stop zjchain@r3.service
sudo systemctl stop zjchain@s1.service
sudo systemctl stop zjchain@s2.service
sudo systemctl stop zjchain@s3.service
