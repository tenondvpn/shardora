#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

sh ./genesis.sh "$1"
cp -f fetch.sh /root && cd /root

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
# 35
cd /root/deploy && sh start.sh r1
sleep3

#30
cd /root/deploy && sh start.sh r2 r3


# 36 #3 #4
cd /root/deploy && sh start.sh s1 s4 s5


# 33
cd /root/deploy && sh start.sh s2 s6 s7

# 32
cd /root/deploy && sh start.sh s3 s8 s9

# 31
cd /root/deploy && sh start.sh s10 s11





