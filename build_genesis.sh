
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
	sudo mv -f /root/zjnodes/zjchain /mnt/
else
	sudo mv -f /root/zjnodes/zjchain /mnt/
fi

sudo rm -rf /root/zjnodes
sudo cp -rf ./zjnodes /root
sudo cp -rf ./deploy /root
sudo cp ./fetch.sh /root
rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf /root/zjnodes/zjchain
	sudo mv -f /mnt/zjchain /root/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4")

for node in "${nodes[@]}"; do
    mkdir -p "/root/zjnodes/${node}/log"
    # cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${node}/conf
    # cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${node}/conf
done
cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/zjchain
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/zjchain/conf
mkdir -p /root/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml

# for node in "${nodes[@]}"; do
    # sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/${node}
# done
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain


if test $NO_BUILD = 0
then
    cd /root/zjnodes/zjchain && ./zjchain -U
    cd /root/zjnodes/zjchain && ./zjchain -S 3
    
fi

#for node in "${root[@]}"; do
#	cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${node}/db
#done


#for node in "${shard3[@]}"; do
#	cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${node}/db
#done


# 压缩 zjnodes/zjchain，便于网络传输

clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
clickhouse-client -q "drop table bls_elect_info"
clickhouse-client -q "drop table bls_block_info"
