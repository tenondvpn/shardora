
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

sh build.sh a $TARGET
sudo rm -rf /root/zjnodes
sudo cp -rf ./zjnodes /root
sudo cp -rf ./deploy /root

rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6")
shard4=("s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_6" "s4_7")
shard5=("s5_1" "s5_2" "s5_3")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_6" "s4_7" "s5_1" "s5_2" "s5_3")

for node in "${nodes[@]}"; do
    mkdir -p "/root/zjnodes/${node}/log"
    cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${node}/conf
    cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${node}/conf
done
mkdir -p /root/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml

for node in "${nodes[@]}"; do
    sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/${node}
done
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain

cd /root/zjnodes/zjchain && ./zjchain -U
cd /root/zjnodes/zjchain && ./zjchain -S 3
cd /root/zjnodes/zjchain && ./zjchain -S 4
cd /root/zjnodes/zjchain && ./zjchain -S 5


for node in "${root[@]}"; do
	cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${node}/db
done


for node in "${shard3[@]}"; do
	cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${node}/db
done


for node in "${shard4[@]}"; do
	cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/${node}/db
done


for node in "${shard5[@]}"; do
	cp -rf /root/zjnodes/zjchain/shard_db_5 /root/zjnodes/${node}/db
done


clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
