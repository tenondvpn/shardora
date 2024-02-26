
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
server1=10.101.20.33
target=$1

echo "[$server0]"
# sshpass -p !@#$%^ ssh -o StrictHostKeyChecking=no root@$server0 <<EOF
cd /root/xufei/zjchain && sh ./build_genesis.sh $target
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} $!@#$%^ r1 r3 s3_2 s3_4 s4_1 s4_3 s4_5
# EOF


(
echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} '!@#$%^' r2 s3_1 s3_3 s3_5 s4_2 s4_4
EOF
) &

wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | grep fei | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | grep fei | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 fei> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_1 s3_3 s3_5 s4_2 s4_4; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node fei> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in r3 s3_2 s3_4 s4_1 s4_3 s4_5; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node fei> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
