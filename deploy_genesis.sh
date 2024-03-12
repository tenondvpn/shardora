
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
target=$1
no_build=$2

echo "[$server0]"
# sshpass -p !@#$%^ ssh -o StrictHostKeyChecking=no root@$server0 <<EOF
sh ./build_genesis.sh $target $no_build
cd /root/xf && sh -x fetch.sh 127.0.0.1 ${server0} '!@#$%^' '/root/xf' r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6
# EOF

wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/xf/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 xf> /dev/null 2>&1 &

sleep 3

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6; do
cd /root/xf/zjnodes/$node/ && nohup ASAN_OPTIONS=detect_leaks=1 ./zjchain -f 0 -g 0 $node xf> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
