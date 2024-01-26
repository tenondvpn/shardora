#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

mode=$1
localip=$2

if [[ -z $2 ]]; then
    echo "Error: Second argument is required."
    exit 1
fi

# 生成创世块数据
sh ./genesis.sh $mode

# 替换
cp -f fetch.sh /root
cd /root && sh -x fetch.sh 127.0.0.1 "${localip}" r1 r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 node

# cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 &
# sleep 3

# cd /root/zjnodes/r2/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/r3/ && nohup ./zjchain -f 0 -g 0 &

# cd /root/zjnodes/s1/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s2/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s3/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s4/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s5/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s6/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s7/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s8/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s9/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s10/ && nohup ./zjchain -f 0 -g 0 &
# cd /root/zjnodes/s11/ && nohup ./zjchain -f 0 -g 0 &

# start nodes with daemon
cd /root/deploy && sh start.sh r1
sleep 3

cd /root/deploy && sh start.sh r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11
