
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
sudo cp ./fetch.sh /root/xf
rm -rf /root/xf/zjnodes/*/zjchain /root/xf/zjnodes/*/core* /root/xf/zjnodes/*/log/* /root/xf/zjnodes/*/*db*

if test $NO_BUILD = "nobuild"
then
	sudo rm -rf /root/xf/zjnodes/zjchain
	sudo mv -f /tmp/zjchain /root/xf/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6")

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
