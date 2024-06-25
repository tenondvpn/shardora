
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
	sudo mv -f /home/xl/zjnodes/zjchain /tmp/
else
	sudo mv -f /home/xl/zjnodes/zjchain /tmp/
fi

sudo rm -rf /home/xl/zjnodes
sudo cp -rf ./zjnodes /home/xl
sudo cp -rf ./deploy /home/xl
sudo cp ./fetch.sh /home/xl
rm -rf /home/xl/zjnodes/*/zjchain /home/xl/zjnodes/*/core* /home/xl/zjnodes/*/log/* /home/xl/zjnodes/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf /home/xl/zjnodes/zjchain
	sudo mv -f /tmp/zjchain /home/xl/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10")

for node in "${nodes[@]}"; do
    mkdir -p "/home/xl/zjnodes/${node}/log"
    # cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /home/xl/zjnodes/${node}/conf
    # cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /home/xl/zjnodes/${node}/conf
done
cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /home/xl/zjnodes/zjchain
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /home/xl/zjnodes/zjchain/conf
mkdir -p /home/xl/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /home/xl/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /home/xl/zjnodes/zjchain/genesis.yml

# for node in "${nodes[@]}"; do
    # sudo cp -rf ./cbuild_$TARGET/zjchain /home/xl/zjnodes/${node}
# done
sudo cp -rf ./cbuild_$TARGET/zjchain /home/xl/zjnodes/zjchain


if test $NO_BUILD = 0
then
    cd /home/xl/zjnodes/zjchain && ./zjchain -U
    cd /home/xl/zjnodes/zjchain && ./zjchain -S 3 &
    wait
fi

#for node in "${root[@]}"; do
#	cp -rf /home/xl/zjnodes/zjchain/root_db /home/xl/zjnodes/${node}/db
#done


#for node in "${shard3[@]}"; do
#	cp -rf /home/xl/zjnodes/zjchain/shard_db_3 /home/xl/zjnodes/${node}/db
#done


# 压缩 zjnodes/zjchain，便于网络传输

clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
