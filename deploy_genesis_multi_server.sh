
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
server1=10.101.20.31
server2=10.101.20.32
server3=10.101.20.36
server4=10.101.20.29
server5=10.101.20.33
target=$1
no_build=$2

echo "[$server0]"
# sshpass -p !@#$%^ ssh -o StrictHostKeyChecking=no root@$server0 <<EOF
cd /root/xufei/zjchain && sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} $!@#$%^ r1 s3_2 s3_9 s3_16 s3_23 s3_30 s4_2 s4_9 s4_16 s4_23 s4_30
# EOF


(
echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} '!@#$%^' s3_5 s3_12 s3_19 s3_26 s3_33 s4_5 s4_12 s4_19 s4_26 s4_33
EOF
) &


(
echo "[$server2]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server2} '!@#$%^' r3 s3_4 s3_11 s3_18 s3_25 s3_32 s4_4 s4_11 s4_18 s4_25 s4_32
EOF
) &


(
echo "[$server3]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server3} '!@#$%^' s3_1 s3_8 s3_15 s3_22 s3_29 s4_1 s4_8 s4_15 s4_22 s4_29
EOF
) &


(
echo "[$server4]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server4} '!@#$%^' s3_7 s3_14 s3_21 s3_28 s3_35 s4_7 s4_14 s4_21 s4_28 s4_35
EOF
) &


(
echo "[$server5]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
rm -rf /root/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server5} '!@#$%^' r2 s3_3 s3_10 s3_17 s3_24 s3_31 s4_3 s4_10 s4_17 s4_24 s4_31
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

echo "[$server2]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
ps -ef | grep zjchain | grep fei | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server3]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
ps -ef | grep zjchain | grep fei | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server4]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
ps -ef | grep zjchain | grep fei | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server5]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server5 <<"EOF"
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
for node in s3_5 s3_12 s3_19 s3_26 s3_33 s4_5 s4_12 s4_19 s4_26 s4_33; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node fei> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_4 s3_11 s3_18 s3_25 s3_32 s4_4 s4_11 s4_18 s4_25 s4_32; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node fei> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_8 s3_15 s3_22 s3_29 s4_1 s4_8 s4_15 s4_22 s4_29; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node fei> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_14 s3_21 s3_28 s3_35 s4_7 s4_14 s4_21 s4_28 s4_35; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node fei> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_3 s3_10 s3_17 s3_24 s3_31 s4_3 s4_10 s4_17 s4_24 s4_31; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node fei> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_2 s3_9 s3_16 s3_23 s3_30 s4_2 s4_9 s4_16 s4_23 s4_30; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node fei> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
