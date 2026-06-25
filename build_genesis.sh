
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
	sudo mv -f /root/shardoras/shardora /mnt/
else
	sudo mv -f /root/shardoras/shardora /mnt/
fi

sudo rm -rf /root/shardoras
sudo cp -rf ./shardoras /root
sudo cp -rf ./deploy /root
sudo cp ./fetch.sh /root
rm -rf /root/shardoras/*/shardora /root/shardoras/*/core* /root/shardoras/*/log/* /root/shardoras/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf /root/shardoras/shardora
	sudo mv -f /mnt/shardora /root/shardoras/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4")

for node in "${nodes[@]}"; do
    mkdir -p "/root/shardoras/${node}/log"
    # cp -rf ./shardoras/shardora/GeoLite2-City.mmdb /root/shardoras/${node}/conf
    # cp -rf ./shardoras/shardora/conf/log4cpp.properties /root/shardoras/${node}/conf
done
cp -rf ./shardoras/shardora/GeoLite2-City.mmdb /root/shardoras/shardora
cp -rf ./shardoras/shardora/conf/log4cpp.properties /root/shardoras/shardora/conf
mkdir -p /root/shardoras/shardora/log


sudo cp -rf ./cbuild_$TARGET/shardora /root/shardoras/shardora
sudo cp -f ./conf/genesis.yml /root/shardoras/shardora/genesis.yml

# for node in "${nodes[@]}"; do
    # sudo cp -rf ./cbuild_$TARGET/shardora /root/shardoras/${node}
# done
sudo cp -rf ./cbuild_$TARGET/shardora /root/shardoras/shardora


if test $NO_BUILD = 0
then
    cd /root/shardoras/shardora && ./shardora -U
    cd /root/shardoras/shardora && ./shardora -S 3
    
fi

#for node in "${root[@]}"; do
#	cp -rf /root/shardoras/shardora/root_db /root/shardoras/${node}/db
#done


#for node in "${shard3[@]}"; do
#	cp -rf /root/shardoras/shardora/shard_db_3 /root/shardoras/${node}/db
#done


# 压缩 shardoras/shardora，便于网络传输

clickhouse-client -q "drop table shardora_ck_account_key_value_table"
clickhouse-client -q "drop table shardora_ck_account_table"
clickhouse-client -q "drop table shardora_ck_block_table"
clickhouse-client -q "drop table shardora_ck_statistic_table"
clickhouse-client -q "drop table shardora_ck_transaction_table"
clickhouse-client -q "drop table bls_elect_info"
clickhouse-client -q "drop table bls_block_info"

killall -9 txcli
