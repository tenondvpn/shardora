for ((i=10000000;i<=99999999;i++));
do
    echo $i
    ps -ef | grep s3_2 | awk -F' ' '{print $2}' | xargs kill -9
    sleep 3
    cd /root/zjnodes/s3_2 && nohup ./zjchain -f 0 -g 0 s3_2 root &
    sleep_time=`expr $RANDOM % 160 + 10`
    echo "sleep time: $sleep_time"
    sleep $sleep_time
done
# node c2c.js 0 38d2a932186ba9f9b2aa74c4c1ee8090a51b49a0 190000
