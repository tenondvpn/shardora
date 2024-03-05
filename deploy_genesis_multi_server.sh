
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
server1=10.101.20.33
server2=10.101.20.31
server3=10.101.20.36
server4=10.101.20.32
target=$1
no_build=$2

echo "[$server0]"
# sshpass -p !@#$%^ ssh -o StrictHostKeyChecking=no root@$server0 <<EOF
cd /root/xufei/zjchain && sh ./build_genesis.sh $target $no_build
cd /root/xf && sh -x fetch.sh 127.0.0.1 ${server0} $!@#$%^ r1 s3_4 s3_9 s3_14 s3_19 s3_24 s3_29 s3_34 s3_39 s3_44 s3_49
# EOF


(
echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server1} '!@#$%^' r3 s3_5 s3_10 s3_15 s3_20 s3_25 s3_30 s3_35 s3_40 s3_45 s3_50
EOF
) &


(
echo "[$server2]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server2} '!@#$%^' s3_2 s3_7 s3_12 s3_17 s3_22 s3_27 s3_32 s3_37 s3_42 s3_47
EOF
) &


(
echo "[$server3]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server3} '!@#$%^' r2 s3_3 s3_8 s3_13 s3_18 s3_23 s3_28 s3_33 s3_38 s3_43 s3_48
EOF
) &


(
echo "[$server4]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p '!@#$%^' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server4} '!@#$%^' s3_1 s3_6 s3_11 s3_16 s3_21 s3_26 s3_31 s3_36 s3_41 s3_46
EOF
) &

wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server2]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server3]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server4]"
sshpass -p '!@#$%^' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/xf/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_5 s3_10 s3_15 s3_20 s3_25 s3_30 s3_35 s3_40 s3_45 s3_50; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_7 s3_12 s3_17 s3_22 s3_27 s3_32 s3_37 s3_42 s3_47; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_3 s3_8 s3_13 s3_18 s3_23 s3_28 s3_33 s3_38 s3_43 s3_48; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p '!@#$%^' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_6 s3_11 s3_16 s3_21 s3_26 s3_31 s3_36 s3_41 s3_46; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_4 s3_9 s3_14 s3_19 s3_24 s3_29 s3_34 s3_39 s3_44 s3_49; do
cd /root/xf/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
