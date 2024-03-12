
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
	sudo mv -f /root/zjnodes/zjchain /tmp/
fi

sudo rm -rf /root/zjnodes
sudo cp -rf ./zjnodes /root
sudo cp -rf ./deploy /root
sudo cp ./fetch.sh /root
rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

if test $NO_BUILD = "nobuild"
then
	sudo rm -rf /root/zjnodes/zjchain
	sudo mv -f /tmp/zjchain /root/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10")

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


if test $NO_BUILD = 0
then
    cd /root/zjnodes/zjchain && ./zjchain -U
    cd /root/zjnodes/zjchain && ./zjchain -S 3 &
    wait
fi

for node in "${root[@]}"; do
	cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${node}/db
done


for node in "${shard3[@]}"; do
	cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${node}/db
done


clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
