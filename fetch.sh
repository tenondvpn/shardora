#!/bin/bash
rm -rf /root/zjnodes
rm -rf /root/deploy

scp -r root@10.101.20.35:/root/zjnodes /root/zjnodes
scp -r root@10.101.20.35:/root/deploy /root/deploy

