
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

NO_BUILD=""
if test $2 = "nobuild"
then
	NO_BUILD="nobuild"
fi

if test $NO_BUILD = ""
then
	sh build.sh a $TARGET	
else
	sudo mv /root/zjnodes/zjchain /tmp
fi

sudo rm -rf /root/zjnodes
sudo cp -rf ./zjnodes /root
sudo cp -rf ./deploy /root
rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

if test $NO_BUILD = "nobuild"
then
	sudo rm -rf /root/zjnodes/zjchain
	sudo mv /tmp/zjchain /root/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_35")
shard4=("s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_7" "s4_8" "s4_9" "s4_10" "s4_11" "s4_12" "s4_14" "s4_15" "s4_16" "s4_17" "s4_18" "s4_19" "s4_21" "s4_22" "s4_23" "s4_24" "s4_25" "s4_26" "s4_28" "s4_29" "s4_30" "s4_31" "s4_32" "s4_33" "s4_35")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_35" "s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_7" "s4_8" "s4_9" "s4_10" "s4_11" "s4_12" "s4_14" "s4_15" "s4_16" "s4_17" "s4_18" "s4_19" "s4_21" "s4_22" "s4_23" "s4_24" "s4_25" "s4_26" "s4_28" "s4_29" "s4_30" "s4_31" "s4_32" "s4_33" "s4_35")

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


if test $NO_BUILD = ""
then
    cd /root/zjnodes/zjchain && ./zjchain -U
    cd /root/zjnodes/zjchain && ./zjchain -S 3 &
    cd /root/zjnodes/zjchain && ./zjchain -S 4 &
    wait
fi

for node in "${root[@]}"; do
	cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${node}/db
done


for node in "${shard3[@]}"; do
	cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${node}/db
done


for node in "${shard4[@]}"; do
	cp -rf /root/zjnodes/zjchain/shard_db_4 /root/zjnodes/${node}/db
done


clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
