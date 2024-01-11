#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
	TARGET=Debug
fi

sh build.sh a $TARGET
sudo cp -rf ./zjnodes /root
sudo cp -rf ./deploy /root

rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*
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

sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s1
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s2
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s3
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s4
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s5
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s6
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s7
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s8
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s9
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s10
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s11

sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/r1
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/r2
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/r3


cd /root/zjnodes/zjchain && ./zjchain -U
cd /root/zjnodes/zjchain && ./zjchain -S 3
cd /root/zjnodes/zjchain && ./zjchain -S 4

cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r1/db
cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r2/db
cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r3/db

cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/s1/db
cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/s2/db
cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/s3/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s4/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s5/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s6/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s7/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s8/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s9/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s10/db
cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/s11/db

clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"

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
cd /root/deploy && sh start.sh
