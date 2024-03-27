
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

# nobuild: no build & no genesis block
# noblock: build & no genesis block
NO_BUILD=0
if [ -n $2 ] && [ $2 = "nobuild" ]
then
    NO_BUILD="nobuild"
fi

if [ -n $2 ] && [ $2 = "noblock" ]
then
    NO_BUILD="noblock"
fi

if test $NO_BUILD = 0
then
	sh build.sh a $TARGET	
elif test $NO_BUILD = "noblock"
then
	sh build.sh a $TARGET
	sudo mv -f /root/xf/zjnodes/zjchain /tmp/
else
	sudo mv -f /root/xf/zjnodes/zjchain /tmp/
fi

sudo rm -rf /root/xf/zjnodes
sudo cp -rf ./zjnodes /root/xf
sudo cp -rf ./deploy /root/xf
sudo cp ./fetch.sh /root/xf
rm -rf /root/xf/zjnodes/*/zjchain /root/xf/zjnodes/*/core* /root/xf/zjnodes/*/log/* /root/xf/zjnodes/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf /root/xf/zjnodes/zjchain
	sudo mv -f /tmp/zjchain /root/xf/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40")
shard4=("s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_6" "s4_7" "s4_8" "s4_9" "s4_10" "s4_11" "s4_12" "s4_13" "s4_14" "s4_15" "s4_16" "s4_17" "s4_18" "s4_19" "s4_20" "s4_21" "s4_22" "s4_23" "s4_24" "s4_25" "s4_26" "s4_27" "s4_28" "s4_29" "s4_30" "s4_31" "s4_32" "s4_33" "s4_34" "s4_35" "s4_36" "s4_37" "s4_38" "s4_39" "s4_40")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_6" "s4_7" "s4_8" "s4_9" "s4_10" "s4_11" "s4_12" "s4_13" "s4_14" "s4_15" "s4_16" "s4_17" "s4_18" "s4_19" "s4_20" "s4_21" "s4_22" "s4_23" "s4_24" "s4_25" "s4_26" "s4_27" "s4_28" "s4_29" "s4_30" "s4_31" "s4_32" "s4_33" "s4_34" "s4_35" "s4_36" "s4_37" "s4_38" "s4_39" "s4_40")

for node in "${nodes[@]}"; do
    mkdir -p "/root/xf/zjnodes/${node}/log"
    # cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/${node}/conf
    # cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/${node}/conf
done
cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/zjchain
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/zjchain/conf
mkdir -p /root/xf/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/xf/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/xf/zjnodes/zjchain/genesis.yml

# for node in "${nodes[@]}"; do
    # sudo cp -rf ./cbuild_$TARGET/zjchain /root/xf/zjnodes/${node}
# done
sudo cp -rf ./cbuild_$TARGET/zjchain /root/xf/zjnodes/zjchain


if test $NO_BUILD = 0
then
    cd /root/xf/zjnodes/zjchain && ./zjchain -U
    cd /root/xf/zjnodes/zjchain && ./zjchain -S 3 &
    cd /root/xf/zjnodes/zjchain && ./zjchain -S 4 &
    wait
fi

#for node in "${root[@]}"; do
#	cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/${node}/db
#done


#for node in "${shard3[@]}"; do
#	cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/${node}/db
#done


#for node in "${shard4[@]}"; do
#	cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/${node}/db
#done


clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
