#!/bin/bash
src_path=`pwd`
cd /root/shardoras/shardora && ./shardora -U -1 67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016:127.0.0.1:1,356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe:127.0.0.1:2,a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee:127.0.0.1:2 -2 e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269:127.0.0.1:3,b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0:127.0.0.1:4,0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995:127.0.0.1:5
cd /root/shardoras/shardora && ./shardora -S -1 67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016:127.0.0.1:1,356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe:127.0.0.1:2,a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee:127.0.0.1:2 -2 e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269:127.0.0.1:3,b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0:127.0.0.1:4,0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995:127.0.0.1:5

cd $src_path
sh ./cmd.sh "ps -ef | grep shardora | awk -F' ' '{print $2}' | xargs kill -9"
sh ./cmd.sh "rm  -rf /root/shardoras/s*/log/*"
sh ./cmd.sh "rm  -rf /root/shardoras/r*/log/*"
sh ./cmd.sh "rm  -rf /root/shardoras/s*/db"
sh ./cmd.sh "rm  -rf /root/shardoras/r*/db"
sh ./cpr.sh /root/shardoras/shardora/root_db /root/shardoras/r1/db &
sh ./cpr.sh /root/shardoras/shardora/root_db /root/shardoras/r2/db &
sh ./cpr.sh /root/shardoras/shardora/root_db /root/shardoras/r3/db &

sh ./cpr.sh /root/shardoras/shardora/shard_db /root/shardoras/s1/db &
sh ./cpr.sh /root/shardoras/shardora/shard_db /root/shardoras/s2/db &
sh ./cpr.sh /root/shardoras/shardora/shard_db /root/shardoras/s3/db &
sh ./cpr.sh /root/shardoras/shardora/shard_db /root/shardoras/s4/db &
sh ./cpr.sh /root/shardoras/shardora/shard_db /root/shardoras/s5/db &
sh ./cpr.sh /root/shardoras/shardora/shard_db /root/shardoras/s6/db &

exit 0
cd /root/shardoras/r1/ && nohup ./shardora -f 1 -g 0 &
sleep 3

cd /root/shardoras/r2/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/r3/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/r4/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/r5/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/r6/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/r7/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s1/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s2/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s3/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s4/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s5/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s6/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s7/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s8/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s9/ && nohup ./shardora -f 0 -g 0 &
#cd /root/shardoras/s10/ && nohup ./shardora -f 0 -g 0 &

exit 0
cd /root/n2 &&  rm -rf db ./log/* && nohup ./shardora2 -f 0 -g 0 &
cd /root/n3 &&  rm -rf db ./log/* && nohup ./shardora3 -f 0 -g 0 &
cd /root/n4 &&  rm -rf db ./log/* && nohup ./shardora4 -f 0 -g 0 &
sleep 3
cd /root/shardoras/s11/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s12/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s13/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s14/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s15/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s16/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s17/ && nohup ./shardora -f 0 -g 0 &
cd /root/shardoras/s18/ && nohup ./shardora -f 0 -g 0 &
