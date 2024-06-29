#!/bin/bash
src_path=`pwd`
cd /root/zjnodes/zjchain && ./zjchain -U -1 67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016:127.0.0.1:1,356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe:127.0.0.1:2,a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee:127.0.0.1:2 -2 e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269:127.0.0.1:3,b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0:127.0.0.1:4,0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995:127.0.0.1:5
cd /root/zjnodes/zjchain && ./zjchain -S -1 67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016:127.0.0.1:1,356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe:127.0.0.1:2,a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee:127.0.0.1:2 -2 e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269:127.0.0.1:3,b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0:127.0.0.1:4,0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995:127.0.0.1:5

cd $src_path
sh ./cmd.sh "ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9"
sh ./cmd.sh "rm  -rf /root/zjnodes/s*/log/*"
sh ./cmd.sh "rm  -rf /root/zjnodes/r*/log/*"
sh ./cmd.sh "rm  -rf /root/zjnodes/s*/db"
sh ./cmd.sh "rm  -rf /root/zjnodes/r*/db"
sh ./cpr.sh /root/zjnodes/zjchain/root_db /root/zjnodes/r1/db &
sh ./cpr.sh /root/zjnodes/zjchain/root_db /root/zjnodes/r2/db &
sh ./cpr.sh /root/zjnodes/zjchain/root_db /root/zjnodes/r3/db &

sh ./cpr.sh /root/zjnodes/zjchain/shard_db /root/zjnodes/s1/db &
sh ./cpr.sh /root/zjnodes/zjchain/shard_db /root/zjnodes/s2/db &
sh ./cpr.sh /root/zjnodes/zjchain/shard_db /root/zjnodes/s3/db &
sh ./cpr.sh /root/zjnodes/zjchain/shard_db /root/zjnodes/s4/db &
sh ./cpr.sh /root/zjnodes/zjchain/shard_db /root/zjnodes/s5/db &
sh ./cpr.sh /root/zjnodes/zjchain/shard_db /root/zjnodes/s6/db &

exit 0
cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 &
sleep 3

cd /root/zjnodes/r2/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/r3/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r4/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r5/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r6/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r7/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s1/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s2/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s3/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s4/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s5/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s6/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s7/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s8/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s9/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s10/ && nohup ./zjchain -f 0 -g 0 &

exit 0
cd /root/n2 &&  rm -rf db ./log/* && nohup ./zjc2 -f 0 -g 0 &
cd /root/n3 &&  rm -rf db ./log/* && nohup ./zjc3 -f 0 -g 0 &
cd /root/n4 &&  rm -rf db ./log/* && nohup ./zjc4 -f 0 -g 0 &
sleep 3
cd /root/zjnodes/s11/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s12/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s13/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s14/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s15/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s16/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s17/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s18/ && nohup ./zjchain -f 0 -g 0 &
