
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=127.0.0.1
target=$1
no_build=$2

echo "[$server0]"
sh ./xl_build_genesis.sh $target $no_build
cd /home/xl && sh -x fetch.sh 127.0.0.1 ${server0} '' '/home/xl' r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10
echo "==== 同步中继服务器 ====" 
wait

(
echo "[$server0]"
for n in r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10; do
    ln -s /home/xl/zjnodes/zjchain/GeoLite2-City.mmdb /home/xl/zjnodes/${n}/conf
    ln -s /home/xl/zjnodes/zjchain/conf/log4cpp.properties /home/xl/zjnodes/${n}/conf
    ln -s /home/xl/zjnodes/zjchain/zjchain /home/xl/zjnodes/${n}
done
) &

(

for n in r1 r2 r3; do
    cp -rf /home/xl/zjnodes/zjchain/root_db /home/xl/zjnodes/${n}/db
done

for n in s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10; do
    cp -rf /home/xl/zjnodes/zjchain/shard_db_3 /home/xl/zjnodes/${n}/db
done
) &
wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | grep xl | awk -F' ' '{print $2}' | xargs kill -9

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /home/xl/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 xl> /dev/null 2>&1 &

sleep 3

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10; do
cd /home/xl/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node xl> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
