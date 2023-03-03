#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
sh build.sh a
cp -r ./zjnodes /root

rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db
mkdir -p /root/zjnodes/zjchain/log
mkdir -p /root/zjnodes/s1/log
mkdir -p /root/zjnodes/s2/log
mkdir -p /root/zjnodes/s3/log
mkdir -p /root/zjnodes/s4/log
mkdir -p /root/zjnodes/s5/log
mkdir -p /root/zjnodes/s6/log
mkdir -p /root/zjnodes/s7/log
mkdir -p /root/zjnodes/s8/log
mkdir -p /root/zjnodes/s9/log
mkdir -p /root/zjnodes/s10/log
mkdir -p /root/zjnodes/s11/log
mkdir -p /root/zjnodes/s12/log
mkdir -p /root/zjnodes/s13/log
mkdir -p /root/zjnodes/s14/log
mkdir -p /root/zjnodes/s15/log
mkdir -p /root/zjnodes/s16/log
mkdir -p /root/zjnodes/s17/log
mkdir -p /root/zjnodes/s18/log
mkdir -p /root/zjnodes/r1/log
mkdir -p /root/zjnodes/r2/log
mkdir -p /root/zjnodes/r3/log
mkdir -p /root/zjnodes/r4/log
mkdir -p /root/zjnodes/r5/log
mkdir -p /root/zjnodes/r6/log
mkdir -p /root/zjnodes/r7/log

cp -rf ./cbuild_Debug/zjchain /root/zjnodes/zjchain
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s1
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s2
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s3
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s4
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s5
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s6
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s7
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s8
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s9
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s10
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s11
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s12
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s13
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s14
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s15
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s16
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s17
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/s18
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r1
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r2
cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r3
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r4
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r5
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r6
#cp -rf ./cbuild_Debug/zjchain /root/zjnodes/r7

cd /root/zjnodes/zjchain && ./zjchain -U -1 031d29587f946b7e57533725856e3b2fc840ac8395311fea149642334629cd5757:127.0.0.1:1,03a6f3b7a4a3b546d515bfa643fc4153b86464543a13ab5dd05ce6f095efb98d87:127.0.0.1:2,031e886027cdf3e7c58b9e47e8aac3fe67c393a155d79a96a0572dd2163b4186f0:127.0.0.1:2 -2 0315a968643f2ada9fd24f0ca92ae5e57d05226cfe7c58d959e510b27628c1cac0:127.0.0.1:3,030d62d31adf3ccbc6283727e2f4493a9228ef80f113504518c7cae46931115138:127.0.0.1:4,028aa5aec8f1cbcd995ffb0105b9c59fd76f29eaffe55521aad4f7a54e78f01e58:127.0.0.1:5
cd /root/zjnodes/zjchain && ./zjchain -S

cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r1/db
cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r2/db
cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r3/db
#cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r4/db
#cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r5/db
#cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r6/db
#cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r7/db
cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s1/db
cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s2/db
cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s3/db
cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s4/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s5/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s6/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s7/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s8/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s9/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s10/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s11/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s12/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s13/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s14/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s15/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s16/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s17/db
#cp -rf /root/zjnodes/zjchain/shard_db /root/zjnodes/s18/db
exit 0

cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 &
sleep 3

cd /root/zjnodes/r2/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/r3/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/r4/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r5/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r6/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/r7/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s1/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s2/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s3/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s4/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s5/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s6/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s7/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s8/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s9/ && nohup ./zjchain -f 0 -g 0 &
#cd /root/zjnodes/s10/ && nohup ./zjchain -f 0 -g 0 &

clickhouse-client -q "drop table tenon_ck_account_key_value_table"
clickhouse-client -q "drop table tenon_ck_account_table"
clickhouse-client -q "drop table tenon_ck_block_table"
clickhouse-client -q "drop table tenon_ck_statistic_table"
clickhouse-client -q "drop table tenon_ck_transaction_table"
exit 0
cd /root/n2 &&  rm -rf db ./log/* && nohup ./tenon2 -f 0 -g 0 &
cd /root/n3 &&  rm -rf db ./log/* && nohup ./tenon3 -f 0 -g 0 &
cd /root/n4 &&  rm -rf db ./log/* && nohup ./tenon4 -f 0 -g 0 &
sleep 3
cd /root/zjnodes/s11/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s12/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s13/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s14/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s15/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s16/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s17/ && nohup ./zjchain -f 0 -g 0 &
cd /root/zjnodes/s18/ && nohup ./zjchain -f 0 -g 0 &
