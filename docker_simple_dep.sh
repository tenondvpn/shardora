killall -9 shardora
killall -9 txcli

TARGET=$2
#VALGRIND='valgrind --log-file=./valgrind_report.log --leak-check=full --show-leak-kinds=all --show-reachable=no --track-origins=yes'
VALGRIND=''
local_ip=$(ip -4 addr show scope global | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | head -n 1)
#sh build.sh a $TARGET
rm -rf /root/shardoras
cp -rf ./shardoras_local /root/shardoras
rm -rf /root/shardoras/*/shardora /root/shardoras/*/core* /root/shardoras/*/log/* /root/shardoras/*/*db*

cp -rf ./shardoras_local/shardora/GeoLite2-City.mmdb /root/shardoras/shardora
cp -rf ./shardoras_local/shardora/conf/log4cpp.properties /root/shardoras/shardora/conf
mkdir -p /root/shardoras/shardora/log


cp -rf ./cbuild_$TARGET/shardora /root/shardoras/shardora
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
cd /root/shardoras/shardora && ./shardora -U -N $nodes_count
cd /root/shardoras/shardora && ./shardora -S 3 -N $nodes_count

rm -rf /root/shardoras/r*
rm -rf /root/shardoras/s*
rm -rf /root/shardoras/new*
rm -rf /root/shardoras/node
rm -rf /root/shardoras/param

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
    cp -rf /root/shardoras/temp /root/shardoras/r$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/shardoras/r$i/conf/shardora.conf
    sed -i 's/LOCAL_PORT/1200'$i'/g' /root/shardoras/r$i/conf/shardora.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/shardoras/r$i/conf/shardora.conf
    sed -i 's/HTTP_PORT/'0'/g' /root/shardoras/r$i/conf/shardora.conf
    sed -i 's/LOCAL_IP/'$local_ip'/g' /root/shardoras/r$i/conf/shardora.conf
    ln /root/shardoras/shardora/shardora /root/shardoras/r$i/shardora
    ln /root/shardoras/shardora/conf/GeoLite2-City.mmdb /root/shardoras/r$i/conf/GeoLite2-City.mmdb
    ln /root/shardoras/shardora/conf/log4cpp.properties /root/shardoras/r$i/conf/log4cpp.properties
    cp -rf /root/shardoras/shardora/root_db /root/shardoras/r$i/db
    mkdir -p /root/shardoras/r$i/log
    cd /root/shardoras/r$i/ && nohup ./shardora -f 0 -g 0 r$i &
    if [ $i -eq 1 ];then
        echo "first node waiting..."
        sleep 3
    fi
done


for ((i=1; i<=$shard3_node_count;i++)); do
    prikey=`sed -n "$i""p" /root/shardora/shards3 | awk -F'\t' '{print $1}'`
    pubkey=`sed -n "$i""p" /root/shardora/shards3 | awk -F'\t' '{print $2}'`
    echo $prikey
    cp -rf /root/shardoras/temp /root/shardoras/s3_$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/shardoras/s3_$i/conf/shardora.conf
    sed -i 's/LOCAL_IP/'$local_ip'/g' /root/shardoras/s3_$i/conf/shardora.conf
    sed -i 's/BOOTSTRAP/'$bootstrap'/g' /root/shardoras/s3_$i/conf/shardora.conf
    if ((i>=100)); then
        sed -i 's/HTTP_PORT/23'$i'/g' /root/shardoras/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/13'$i'/g' /root/shardoras/s3_$i/conf/shardora.conf
    elif ((i>=10)); then
        sed -i 's/HTTP_PORT/230'$i'/g' /root/shardoras/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/130'$i'/g' /root/shardoras/s3_$i/conf/shardora.conf 
    else
        sed -i 's/HTTP_PORT/2300'$i'/g' /root/shardoras/s3_$i/conf/shardora.conf
        sed -i 's/LOCAL_PORT/1300'$i'/g' /root/shardoras/s3_$i/conf/shardora.conf 
    fi

    ln /root/shardoras/shardora/shardora /root/shardoras/s3_$i/shardora
    ln /root/shardoras/shardora/conf/GeoLite2-City.mmdb /root/shardoras/s3_$i/conf/GeoLite2-City.mmdb
    ln /root/shardoras/shardora/conf/log4cpp.properties /root/shardoras/s3_$i/conf/log4cpp.properties
    mkdir -p /root/shardoras/s3_$i/log
    cp -rf /root/shardoras/shardora/shard_db_3 /root/shardoras/s3_$i/db
    cd /root/shardoras/s3_$i/ && nohup $VALGRIND ./shardora -f 0 -g 0 s3_$i &
    sleep 0.3
done
