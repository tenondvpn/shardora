
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
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10")
shard4=("s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_6" "s4_7" "s4_8" "s4_9" "s4_10")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s4_1" "s4_2" "s4_3" "s4_4" "s4_5" "s4_6" "s4_7" "s4_8" "s4_9" "s4_10")

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
