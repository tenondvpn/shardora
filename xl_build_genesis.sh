
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
	sudo mv -f /home/xl/zjnodes/shardora /tmp/
else
	sudo mv -f /home/xl/zjnodes/shardora /tmp/
fi

sudo rm -rf /home/xl/zjnodes
sudo cp -rf ./zjnodes /home/xl
sudo cp -rf ./deploy /home/xl
sudo cp ./fetch.sh /home/xl
rm -rf /home/xl/zjnodes/*/shardora /home/xl/zjnodes/*/core* /home/xl/zjnodes/*/log/* /home/xl/zjnodes/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf /home/xl/zjnodes/shardora
	sudo mv -f /tmp/shardora /home/xl/zjnodes/shardora
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s3_41" "s3_42" "s3_43" "s3_44" "s3_45" "s3_46" "s3_47" "s3_48" "s3_49" "s3_50")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s3_41" "s3_42" "s3_43" "s3_44" "s3_45" "s3_46" "s3_47" "s3_48" "s3_49" "s3_50")

for node in "${nodes[@]}"; do
    mkdir -p "/home/xl/zjnodes/${node}/log"
    # cp -rf ./zjnodes/shardora/GeoLite2-City.mmdb /home/xl/zjnodes/${node}/conf
    # cp -rf ./zjnodes/shardora/conf/log4cpp.properties /home/xl/zjnodes/${node}/conf
done
cp -rf ./zjnodes/shardora/GeoLite2-City.mmdb /home/xl/zjnodes/shardora
cp -rf ./zjnodes/shardora/conf/log4cpp.properties /home/xl/zjnodes/shardora/conf
mkdir -p /home/xl/zjnodes/shardora/log


sudo cp -rf ./cbuild_$TARGET/shardora /home/xl/zjnodes/shardora/xlchain
sudo cp -f ./conf/genesis.yml /home/xl/zjnodes/shardora/genesis.yml

# for node in "${nodes[@]}"; do
    # sudo cp -rf ./cbuild_$TARGET/shardora /home/xl/zjnodes/${node}/xlchain
# done
sudo cp -rf ./cbuild_$TARGET/shardora /home/xl/zjnodes/shardora/xlchain


if test $NO_BUILD = 0
then
    cd /home/xl/zjnodes/shardora && ./xlchain -U
    cd /home/xl/zjnodes/shardora && ./xlchain -S 3 &
    wait
fi

#for node in "${root[@]}"; do
#	cp -rf /home/xl/zjnodes/shardora/root_db /home/xl/zjnodes/${node}/db
#done


#for node in "${shard3[@]}"; do
#	cp -rf /home/xl/zjnodes/shardora/shard_db_3 /home/xl/zjnodes/${node}/db
#done


# 压缩 zjnodes/shardora，便于网络传输

clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
