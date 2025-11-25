killall -9 zjchain
killall -9 txcli

TEST_TX_TPS=30000
TEST_TX_MAX_POOL_INDEX=0
FOR_CK=0
TARGET=Debug
#VALGRIND='valgrind --log-file=./valgrind_report.log --leak-check=full --show-leak-kinds=all --show-reachable=no --track-origins=yes'
VALGRIND=''
bash build.sh a $TARGET
rm -rf /root/zjnodes
cp -rf ./zjnodes_local /root/zjnodes/
mkdir -p /root/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml

sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
nodes_count=$1
if [[ "$nodes_count" -eq "" ]]; then
   nodes_count=4 
fi
shard3_node_count=`wc -l /root/shardora/shards3 | awk -F' ' '{print $1}'`

if [ "$shard3_node_count" != "$nodes_count" ]; then
    echo "new shard nodes file will create."
    rm -rf /root/shardora/shards*
fi  

echo "node count: " $nodes_count
cd /root/zjnodes/zjchain && ./zjchain -U -N $nodes_count
cd /root/zjnodes/zjchain && ./zjchain -S 3 -N $nodes_count

rm -rf /root/zjnodes/r*
rm -rf /root/zjnodes/s*
rm -rf /root/zjnodes/new*
rm -rf /root/zjnodes/node
rm -rf /root/zjnodes/param

shard3_node_count=`wc -l /root/shardora/shards3 | awk -F' ' '{print $1}'`
root_node_count=`wc -l /root/shardora/root_nodes | awk -F' ' '{print $1}'`
bootstrap=""
echo $shard3_node_count $root_node_count
for ((i=1; i<=$root_node_count;i++)); do
    tmppubkey=`sed -n "$i""p" /root/shardora/root_nodes | awk -F'\t' '{print $2}'`
    node_info=$tmppubkey":127.0.0.1:1200"$i
    bootstrap=$node_info","$bootstrap
done

for ((i=1; i<=3;i++)); do
    tmppubkey=`sed -n "$i""p" /root/shardora/shards3| awk -F'\t' '{print $2}'`
    node_info=$tmppubkey":127.0.0.1:1300"$i
    bootstrap=$node_info","$bootstrap
done

echo $bootstrap
for ((i=1; i<=$root_node_count;i++)); do
    prikey=`sed -n "$i""p" /root/shardora/root_nodes | awk -F'\t' '{print $1}'`
    pubkey=`sed -n "$i""p" /root/shardora/root_nodes | awk -F'\t' '{print $2}'`
    echo $prikey
    cp -rf /root/zjnodes/temp /root/zjnodes/r$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/r$i/conf/zjchain.conf
    sed -i 's/LOCAL_PORT/1200'$i'/g' /root/zjnodes/r$i/conf/zjchain.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/zjnodes/r$i/conf/zjchain.conf
    sed -i 's/HTTP_PORT/'0'/g' /root/zjnodes/r$i/conf/zjchain.conf
    sed -i 's/LOCAL_IP/127.0.0.1/g' /root/zjnodes/r$i/conf/zjchain.conf
    ln /root/zjnodes/zjchain/zjchain /root/zjnodes/r$i/zjchain
    ln /root/zjnodes/zjchain/conf/GeoLite2-City.mmdb /root/zjnodes/r$i/conf/GeoLite2-City.mmdb
    ln /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/r$i/conf/log4cpp.properties
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/r$i/db
    mkdir -p /root/zjnodes/r$i/log
    cd /root/zjnodes/r$i/ && nohup ./zjchain -f 0 -g 0 r$i &
    if [ $i -eq 1 ];then
        echo "first node waiting..."
        sleep 3
    fi
done


for ((i=1; i<=$shard3_node_count;i++)); do
    prikey=`sed -n "$i""p" /root/shardora/shards3 | awk -F'\t' '{print $1}'`
    pubkey=`sed -n "$i""p" /root/shardora/shards3 | awk -F'\t' '{print $2}'`
    echo $prikey
    cp -rf /root/zjnodes/temp /root/zjnodes/s3_$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    sed -i 's/LOCAL_IP/127.0.0.1/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    if ((i<=TEST_TX_MAX_POOL_INDEX)); then
        sed -i 's/TEST_POOL_INDEX/'$(($i-1))'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    else
        sed -i 's/TEST_POOL_INDEX/-1/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    fi

    sed -i 's/TEST_TX_TPS/'$TEST_TX_TPS'/g' /root/zjnodes/s3_$i/conf/zjchain.conf

    if ((i>=100)); then
        sed -i 's/HTTP_PORT/23'$i'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
        sed -i 's/LOCAL_PORT/13'$i'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    elif ((i>=10)); then
        sed -i 's/HTTP_PORT/230'$i'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
        sed -i 's/LOCAL_PORT/130'$i'/g' /root/zjnodes/s3_$i/conf/zjchain.conf 
    else
        sed -i 's/HTTP_PORT/2300'$i'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
        sed -i 's/LOCAL_PORT/1300'$i'/g' /root/zjnodes/s3_$i/conf/zjchain.conf 
    fi
    
    if (($FOR_CK==1 && i==1)); then
        sed -i 's/FOR_CK_CLIENT/true/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    else
        sed -i 's/FOR_CK_CLIENT/false/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    fi
    ln /root/zjnodes/zjchain/zjchain /root/zjnodes/s3_$i/zjchain
    ln /root/zjnodes/zjchain/conf/GeoLite2-City.mmdb /root/zjnodes/s3_$i/conf/GeoLite2-City.mmdb
    ln /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/s3_$i/conf/log4cpp.properties
    mkdir -p /root/zjnodes/s3_$i/log
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/s3_$i/db
    cd /root/zjnodes/s3_$i/ && nohup $VALGRIND ./zjchain -f 0 -g 0 s3_$i &
    sleep 0.3
done
