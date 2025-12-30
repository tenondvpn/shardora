killall -9 shardora
killall -9 txcli

TARGET=$2
#VALGRIND='valgrind --log-file=./valgrind_report.log --leak-check=full --show-leak-kinds=all --show-reachable=no --track-origins=yes'
VALGRIND=''
local_ip=$(ip -4 addr show scope global | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | head -n 1)
#sh build.sh a $TARGET
rm -rf /root/zjnodes
cp -rf ./zjnodes_local /root/zjnodes
rm -rf /root/zjnodes/*/shardora /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

cp -rf ./zjnodes_local/shardora/GeoLite2-City.mmdb /root/zjnodes/shardora
cp -rf ./zjnodes_local/shardora/conf/log4cpp.properties /root/zjnodes/shardora/conf
mkdir -p /root/zjnodes/shardora/log


cp -rf ./cbuild_$TARGET/shardora /root/zjnodes/shardora
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
cd /root/zjnodes/shardora && ./shardora -U -N $nodes_count
cd /root/zjnodes/shardora && ./shardora -S 3 -N $nodes_count

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
    node_info=$tmppubkey":"$local_ip":1200"$i
    bootstrap=$node_info","$bootstrap
done

for ((i=1; i<=3;i++)); do
    tmppubkey=`sed -n "$i""p" /root/shardora/shards3| awk -F'\t' '{print $2}'`
    node_info=$tmppubkey":"$local_ip":1300"$i
    bootstrap=$node_info","$bootstrap
done

echo $bootstrap
for ((i=1; i<=$root_node_count;i++)); do
    prikey=`sed -n "$i""p" /root/shardora/root_nodes | awk -F'\t' '{print $1}'`
    pubkey=`sed -n "$i""p" /root/shardora/root_nodes | awk -F'\t' '{print $2}'`
    echo $prikey
    cp -rf /root/zjnodes/temp /root/zjnodes/r$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/r$i/conf/shardora.conf
    sed -i 's/LOCAL_PORT/1200'$i'/g' /root/zjnodes/r$i/conf/shardora.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/zjnodes/r$i/conf/shardora.conf
    sed -i 's/HTTP_PORT/'0'/g' /root/zjnodes/r$i/conf/shardora.conf
    sed -i 's/LOCAL_IP/'$local_ip'/g' /root/zjnodes/r$i/conf/shardora.conf
    ln /root/zjnodes/shardora/shardora /root/zjnodes/r$i/shardora
    ln /root/zjnodes/shardora/conf/GeoLite2-City.mmdb /root/zjnodes/r$i/conf/GeoLite2-City.mmdb
    ln /root/zjnodes/shardora/conf/log4cpp.properties /root/zjnodes/r$i/conf/log4cpp.properties
    cp -rf /root/zjnodes/shardora/root_db /root/zjnodes/r$i/db
    mkdir -p /root/zjnodes/r$i/log
    cd /root/zjnodes/r$i/ && nohup ./shardora -f 0 -g 0 r$i &
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
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/s3_$i/conf/shardora.conf
    sed -i 's/LOCAL_IP/'$local_ip'/g' /root/zjnodes/s3_$i/conf/shardora.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/zjnodes/s3_$i/conf/shardora.conf
    if ((i>=100)); then
        sed -i 's/HTTP_PORT/23'$i'/g' /root/zjnodes/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/13'$i'/g' /root/zjnodes/s3_$i/conf/shardora.conf
    elif ((i>=10)); then
        sed -i 's/HTTP_PORT/230'$i'/g' /root/zjnodes/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/130'$i'/g' /root/zjnodes/s3_$i/conf/shardora.conf 
    else
        sed -i 's/HTTP_PORT/2300'$i'/g' /root/zjnodes/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/1300'$i'/g' /root/zjnodes/s3_$i/conf/shardora.conf 
    fi

    ln /root/zjnodes/shardora/shardora /root/zjnodes/s3_$i/shardora
    ln /root/zjnodes/shardora/conf/GeoLite2-City.mmdb /root/zjnodes/s3_$i/conf/GeoLite2-City.mmdb
    ln /root/zjnodes/shardora/conf/log4cpp.properties /root/zjnodes/s3_$i/conf/log4cpp.properties
    mkdir -p /root/zjnodes/s3_$i/log
    cp -rf /root/zjnodes/shardora/shard_db_3 /root/zjnodes/s3_$i/db
    cd /root/zjnodes/s3_$i/ && nohup $VALGRIND ./shardora -f 0 -g 0 s3_$i &
    sleep 0.3
done
