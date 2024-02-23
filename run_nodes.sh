
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
server1=10.101.20.33
target=$1

echo "[$server0]"
sshpass -p !@#$%^ ssh root@$server0 <<EOF
cd /root/xufei/zjchain && sh deploy_genesis.sh $target ${server0}
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} $pass r1 r2 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s3_11 s3_12 s3_13 s3_14 s3_15 s3_16 s3_17 s3_18 s3_19 s3_20 s3_21 s3_22 s3_23 s3_24 s3_25 s4_1 s4_2 s4_3 s4_4 s4_5 s4_6 s4_7 s4_8 s4_9 s4_10 s4_11 s4_12 s4_13 s4_14 s4_15 s4_16 s4_17 s4_18 s4_19 s4_20 s4_21 s4_22 s4_23 s4_24 s4_25 s4_26 s4_27 s4_28 s4_29 s4_30 s4_31 s4_32 s4_33 s4_34 s4_35 s4_36 s4_37 s4_38 s4_39 s4_40 s4_41 s4_42 s4_43 s4_44 s4_45 s4_46 s4_47 s4_48 s4_49 s4_50
EOF


echo "[$server1]"
sshpass -p !@#$%^ ssh root@$server1 <<EOF
sshpass -p !@#$%^ scp root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} !@#$%^ r3 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s3_37 s3_38 s3_39 s3_40 s3_41 s3_42 s3_43 s3_44 s3_45 s3_46 s3_47 s3_48 s3_49 s3_50
EOF


echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="


echo "[$server0]"
sshpass -p !@#$%^ ssh root@$server0 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server1]"
sshpass -p !@#$%^ ssh root@$server1 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
sshpass -p !@#$%^ ssh -f root@$server0 "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 > /dev/null 2>&1 &"

sleep 3

sshpass -p !@#$%^ ssh -f root@$server0 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s3_11 s3_12 s3_13 s3_14 s3_15 s3_16 s3_17 s3_18 s3_19 s3_20 s3_21 s3_22 s3_23 s3_24 s3_25 s4_1 s4_2 s4_3 s4_4 s4_5 s4_6 s4_7 s4_8 s4_9 s4_10 s4_11 s4_12 s4_13 s4_14 s4_15 s4_16 s4_17 s4_18 s4_19 s4_20 s4_21 s4_22 s4_23 s4_24 s4_25 s4_26 s4_27 s4_28 s4_29 s4_30 s4_31 s4_32 s4_33 s4_34 s4_35 s4_36 s4_37 s4_38 s4_39 s4_40 s4_41 s4_42 s4_43 s4_44 s4_45 s4_46 s4_47 s4_48 s4_49 s4_50; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


sshpass -p !@#$%^ ssh -f root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s3_37 s3_38 s3_39 s3_40 s3_41 s3_42 s3_43 s3_44 s3_45 s3_46 s3_47 s3_48 s3_49 s3_50; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


echo "==== STEP3: DONE ===="
