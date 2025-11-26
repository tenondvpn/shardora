
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=127.0.0.1
target=$1
no_build=$2

echo "==== STEP0: KILL OLDS ===="
ps -ef | grep shardora | grep root | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} '' '/root' r1 r2 r3 s3_1 s3_2 s3_3 s3_4
echo "==== 同步中继服务器 ====" 
wait

(
echo "[$server0]"
for n in r1 r2 r3 s3_1 s3_2 s3_3 s3_4; do
    ln -s /root/zjnodes/shardora/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/shardora/conf/log4cpp.properties /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/shardora/shardora /root/zjnodes/${n}
done
) &

(

for n in r1 r2 r3; do
    cp -rf /root/zjnodes/shardora/root_db /root/zjnodes/${n}/db
done

for n in s3_1 s3_2 s3_3 s3_4; do
    cp -rf /root/zjnodes/shardora/shard_db_3 /root/zjnodes/${n}/db
done
) &
wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

# ps -ef | grep shardora | grep root | awk -F' ' '{print $2}' | xargs kill -9

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./shardora -f 1 -g 0 r1 root> /dev/null 2>&1 &

sleep 3

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in r2 r3 s3_1 s3_2 s3_3 s3_4; do
cd /root/zjnodes/$node/ && nohup ./shardora -f 0 -g 0 $node root> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
