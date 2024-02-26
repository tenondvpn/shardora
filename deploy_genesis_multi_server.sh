
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
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} $!@#$%^ r1 r2 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s4_1 s4_2 s4_3 s4_4 s4_5 s4_6 s4_7 s4_8 s4_9 s4_10
# EOF


(
echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} '!@#$%^' r3 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s4_26 s4_27 s4_28 s4_29 s4_30 s4_31 s4_32 s4_33 s4_34 s4_35 s4_36
EOF
) &

wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | grep default | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | grep default | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 default> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s4_26 s4_27 s4_28 s4_29 s4_30 s4_31 s4_32 s4_33 s4_34 s4_35 s4_36; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node default> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in r2 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s4_1 s4_2 s4_3 s4_4 s4_5 s4_6 s4_7 s4_8 s4_9 s4_10; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node default> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
