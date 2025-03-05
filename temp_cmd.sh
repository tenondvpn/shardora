local_ip=$1
start_pos=$2
node_count=$3
bootstrap=$4
echo "new node: $local_ip $start_pos $node_count"

exit 0
shard3_node_count=`wc -l /root/pkg/shards3 | awk -F' ' '{print $1}'`
root_node_count=`wc -l /root/pkg/root_nodes | awk -F' ' '{print $1}'`
end_pos=$((start_pos + node_count))
for ((i=$start_pos; i<=$end_pos;i++)); do
    prikey=`sed -n "$i""p" /root/pkg/shards3 | awk -F'\t' '{print $1}'`
    pubkey=`sed -n "$i""p" /root/pkg/shards3 | awk -F'\t' '{print $2}'`
    echo $prikey
    cp -rf /root/zjnodes/temp /root/zjnodes/s3_$i
    sed -i 's/PRIVATE_KEY/'$prikey'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
    sed -i 's/LOCAL_IP/'$local_ip'/g' /root/zjnodes/s3_$i/conf/zjchain.conf
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

    ln /root/pkg/zjchain /root/zjnodes/s3_$i/zjchain
    ln /root/pkg/conf/GeoLite2-City.mmdb /root/zjnodes/s3_$i/conf/GeoLite2-City.mmdb
    ln /root/pkg/conf/log4cpp.properties /root/zjnodes/s3_$i/conf/log4cpp.properties
    mkdir -p /root/zjnodes/s3_$i/log
    cp -rf /root/pkg/shard_db_3 /root/zjnodes/s3_$i/db
    cd /root/zjnodes/s3_$i/ && nohup ./zjchain -f 0 -g 0 s3_$i &
    sleep 1
done
