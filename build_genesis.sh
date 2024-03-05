
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

NO_BUILD=0
if [ -n $2 ] && [ $2 = "nobuild" ]
then
    NO_BUILD="nobuild"
fi

if test $NO_BUILD = 0
then
	sh build.sh a $TARGET	
else
	sudo mv -f /root/xf/zjnodes/zjchain /tmp/
fi

sudo rm -rf /root/xf/zjnodes
sudo cp -rf ./zjnodes /root/xf
sudo cp -rf ./deploy /root/xf
rm -rf /root/xf/zjnodes/*/zjchain /root/xf/zjnodes/*/core* /root/xf/zjnodes/*/log/* /root/xf/zjnodes/*/*db*

if test $NO_BUILD = "nobuild"
then
	sudo rm -rf /root/xf/zjnodes/zjchain
	sudo mv -f /tmp/zjchain /root/xf/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s3_41" "s3_42" "s3_43" "s3_44" "s3_45" "s3_46" "s3_47" "s3_48" "s3_49" "s3_50")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s3_41" "s3_42" "s3_43" "s3_44" "s3_45" "s3_46" "s3_47" "s3_48" "s3_49" "s3_50")

for node in "${nodes[@]}"; do
    mkdir -p "/root/xf/zjnodes/${node}/log"
    cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/${node}/conf
    cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/${node}/conf
done
mkdir -p /root/xf/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/xf/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/xf/zjnodes/zjchain/genesis.yml

for node in "${nodes[@]}"; do
    sudo cp -rf ./cbuild_$TARGET/zjchain /root/xf/zjnodes/${node}
done
sudo cp -rf ./cbuild_$TARGET/zjchain /root/xf/zjnodes/zjchain


if test $NO_BUILD = 0
then
    cd /root/xf/zjnodes/zjchain && ./zjchain -U
    cd /root/xf/zjnodes/zjchain && ./zjchain -S 3 &
    wait
fi

for node in "${root[@]}"; do
	cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/${node}/db
done


for node in "${shard3[@]}"; do
	cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/${node}/db
done


clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
