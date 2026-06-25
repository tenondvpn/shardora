killall -9 shardora
killall -9 shardora
killall -9 txcli

TEST_TX_TPS=30000
TEST_TX_MAX_POOL_INDEX=0
FOR_CK=0
SHARD_END_NETWORK_ID=$2
TARGET=Release
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

if [[ "$SHARD_END_NETWORK_ID" -eq "" ]]; then
   SHARD_END_NETWORK_ID=4
fi

echo "node count: " $nodes_count
rm -rf /root/nodes/shardora/latest_blocks
#cd /root/nodes/shardora && ./shardora -U -N $nodes_count -E $SHARD_END_NETWORK_ID
#cd /root/nodes/shardora && ./shardora -S 3 -N $nodes_count -E $SHARD_END_NETWORK_ID
#cd /root/nodes/shardora && ./shardora -C

bootstrap=""
root_node_count=`wc -l /root/shardora/root_nodes | awk -F' ' '{print $1}'`
for ((net_id=2; net_id<${SHARD_END_NETWORK_ID}; net_id++)); do
    for ((i=1; i<=3;i++)); do
        tmppubkey=`sed -n "$i""p" /root/shardora/shards"$net_id"| awk -F'\t' '{print $2}'`
        node_info=$tmppubkey":127.0.0.1:1"$net_id"00"$i
        bootstrap=$node_info","$bootstrap
    done
done

echo $bootstrap
for ((net_id=2; net_id<${SHARD_END_NETWORK_ID}; net_id++)); do
    shard_node_count=`wc -l /root/shardora/shards${net_id} | awk -F' ' '{print $1}'`
    for ((i=1; i<=$shard_node_count;i++)); do
        prikey=`sed -n "$i""p" /root/shardora/shards${net_id} | awk -F'\t' '{print $1}'`
        pubkey=`sed -n "$i""p" /root/shardora/shards${net_id} | awk -F'\t' '{print $2}'`
        echo $prikey
        cp -rf /root/nodes/temp /root/nodes/s${net_id}_$i
        sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        sed -i 's/LOCAL_IP/127.0.0.1/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        sed -i 's/NETWORK_ID/'$net_id'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        if ((i<=TEST_TX_MAX_POOL_INDEX)); then
            sed -i 's/TEST_POOL_INDEX/'$(($i-1))'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        else
            sed -i 's/TEST_POOL_INDEX/-1/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        fi

        sed -i 's/TEST_TX_TPS/'$TEST_TX_TPS'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf

        if ((i>=100)); then
            sed -i 's/HTTP_PORT/2'${net_id}$i'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
            sed -i 's/LOCAL_PORT/1'${net_id}$i'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        elif ((i>=10)); then
            sed -i 's/HTTP_PORT/2'${net_id}'0'$i'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
            sed -i 's/LOCAL_PORT/1'${net_id}'0'$i'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        else
            sed -i 's/HTTP_PORT/2'${net_id}'00'$i'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
            sed -i 's/LOCAL_PORT/1'${net_id}'00'$i'/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        fi

        if (($FOR_CK==1 && i==1)); then
            sed -i 's/FOR_CK_CLIENT/true/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        else
            sed -i 's/FOR_CK_CLIENT/false/g' /root/nodes/s${net_id}_$i/conf/shardora.conf
        fi
        ln /root/nodes/shardora/shardora /root/nodes/s${net_id}_$i/shardora
        ln /root/nodes/shardora/conf/GeoLite2-City.mmdb /root/nodes/s${net_id}_$i/conf/GeoLite2-City.mmdb
        ln /root/nodes/shardora/conf/log4cpp.properties /root/nodes/s${net_id}_$i/conf/log4cpp.properties
        mkdir -p /root/nodes/s${net_id}_$i/log
        cp -rf /root/nodes/shardora/shard_db_${net_id} /root/nodes/s${net_id}_$i/db
        cd /root/nodes/s${net_id}_$i/ && nohup $VALGRIND ./shardora -f 0 -g 0 s${net_id}_$i &
        sleep 0.3
    done
done
