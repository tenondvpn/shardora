killall -9 shardora
killall -9 txcli

TEST_TX_TPS=30000
TEST_TX_MAX_POOL_INDEX=0
FOR_CK=0
TARGET=Debug
#VALGRIND='valgrind --log-file=./valgrind_report.log --leak-check=full --show-leak-kinds=all --show-reachable=no --track-origins=yes'
VALGRIND=''
bash build.sh a $TARGET
rm -rf /root/nodes
cp -rf ./nodes_local /root/nodes/
mkdir -p /root/nodes/shardora/log


sudo cp -rf ./cbuild_$TARGET/shardora /root/nodes/shardora
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
rm -rf /root/nodes/shardora/latest_blocks
cd /root/nodes/shardora && ./shardora -U -N $nodes_count
cd /root/nodes/shardora && ./shardora -S 3 -N $nodes_count
cd /root/nodes/shardora && ./shardora -C

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
    cp -rf /root/nodes/temp /root/nodes/r$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/nodes/r$i/conf/shardora.conf
    sed -i 's/LOCAL_PORT/1200'$i'/g' /root/nodes/r$i/conf/shardora.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/nodes/r$i/conf/shardora.conf
    sed -i 's/HTTP_PORT/'0'/g' /root/nodes/r$i/conf/shardora.conf
    sed -i 's/LOCAL_IP/127.0.0.1/g' /root/nodes/r$i/conf/shardora.conf
    ln /root/nodes/shardora/shardora /root/nodes/r$i/shardora
    ln /root/nodes/shardora/conf/GeoLite2-City.mmdb /root/nodes/r$i/conf/GeoLite2-City.mmdb
    ln /root/nodes/shardora/conf/log4cpp.properties /root/nodes/r$i/conf/log4cpp.properties
    cp -rf /root/nodes/shardora/root_db /root/nodes/r$i/db
    mkdir -p /root/nodes/r$i/log
    cd /root/nodes/r$i/ && nohup ./shardora -f 0 -g 0 r$i &
    if [ $i -eq 1 ];then
        echo "first node waiting..."
        sleep 3
    fi
done


for ((i=1; i<=$shard3_node_count;i++)); do
    prikey=`sed -n "$i""p" /root/shardora/shards3 | awk -F'\t' '{print $1}'`
    pubkey=`sed -n "$i""p" /root/shardora/shards3 | awk -F'\t' '{print $2}'`
    echo $prikey
    cp -rf /root/nodes/temp /root/nodes/s3_$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/nodes/s3_$i/conf/shardora.conf
    sed -i 's/LOCAL_IP/127.0.0.1/g' /root/nodes/s3_$i/conf/shardora.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/nodes/s3_$i/conf/shardora.conf
    if ((i<=TEST_TX_MAX_POOL_INDEX)); then
        sed -i 's/TEST_POOL_INDEX/'$(($i-1))'/g' /root/nodes/s3_$i/conf/shardora.conf
    else
        sed -i 's/TEST_POOL_INDEX/-1/g' /root/nodes/s3_$i/conf/shardora.conf
    fi

    sed -i 's/TEST_TX_TPS/'$TEST_TX_TPS'/g' /root/nodes/s3_$i/conf/shardora.conf

    if ((i>=100)); then
        sed -i 's/HTTP_PORT/23'$i'/g' /root/nodes/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/13'$i'/g' /root/nodes/s3_$i/conf/shardora.conf
    elif ((i>=10)); then
        sed -i 's/HTTP_PORT/230'$i'/g' /root/nodes/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/130'$i'/g' /root/nodes/s3_$i/conf/shardora.conf 
    else
        sed -i 's/HTTP_PORT/2300'$i'/g' /root/nodes/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/1300'$i'/g' /root/nodes/s3_$i/conf/shardora.conf 
    fi
    
    if (($FOR_CK==1 && i==1)); then
        sed -i 's/FOR_CK_CLIENT/true/g' /root/nodes/s3_$i/conf/shardora.conf
    else
        sed -i 's/FOR_CK_CLIENT/false/g' /root/nodes/s3_$i/conf/shardora.conf
    fi
    ln /root/nodes/shardora/shardora /root/nodes/s3_$i/shardora
    ln /root/nodes/shardora/conf/GeoLite2-City.mmdb /root/nodes/s3_$i/conf/GeoLite2-City.mmdb
    ln /root/nodes/shardora/conf/log4cpp.properties /root/nodes/s3_$i/conf/log4cpp.properties
    mkdir -p /root/nodes/s3_$i/log
    cp -rf /root/nodes/shardora/shard_db_3 /root/nodes/s3_$i/db
    cd /root/nodes/s3_$i/ && nohup $VALGRIND ./shardora -f 0 -g 0 s3_$i &
    sleep 0.3
done
