#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
	TARGET=Debug
fi

sh build.sh a $1
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

cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s1
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s2
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s3
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s4
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/s5

cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/r1
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/r2
cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/r3
#cp -rf ./cbuild_Release/zjchain /root/zjnodes/r4
#cp -rf ./cbuild_Release/zjchain /root/zjnodes/r5
#cp -rf ./cbuild_Release/zjchain /root/zjnodes/r6
#cp -rf ./cbuild_Release/zjchain /root/zjnodes/r7

cd /root/zjnodes/zjchain && ./zjchain -U -1 67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016:127.0.0.1:1,356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe:127.0.0.1:2,a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee:127.0.0.1:2 -2 e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269:127.0.0.1:3,b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0:127.0.0.1:4,0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995:127.0.0.1:5
cd /root/zjnodes/zjchain && ./zjchain -S -1 67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016:127.0.0.1:1,356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe:127.0.0.1:2,a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee:127.0.0.1:2 -2 e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269:127.0.0.1:3,b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0:127.0.0.1:4,0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995:127.0.0.1:5

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
cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/s4/db
cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/s5/db
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

# start nodes with daemon
sh deploy/start.sh
