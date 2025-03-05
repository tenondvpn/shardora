local_ip=$1
start_pos=$2
node_count=$3
echo "new node: $local_ip $start_pos $node_count"
exit 0
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

    ln /root/zjnodes/zjchain/zjchain /root/zjnodes/s3_$i/zjchain
    ln /root/zjnodes/zjchain/conf/GeoLite2-City.mmdb /root/zjnodes/s3_$i/conf/GeoLite2-City.mmdb
    ln /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/s3_$i/conf/log4cpp.properties
    mkdir -p /root/zjnodes/s3_$i/log
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/s3_$i/db
    cd /root/zjnodes/s3_$i/ && nohup ./zjchain -f 0 -g 0 s3_$i &
    sleep 1
done
