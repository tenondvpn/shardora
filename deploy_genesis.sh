
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.16
server1=10.0.0.18
server2=10.0.0.19
server3=10.0.0.20
server4=10.0.0.17
server5=10.0.0.21
target=$1
no_build=$2

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root/xf && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root/xf' r1 s3_4 s3_10 s3_16 s3_22 s3_28 s3_34 s3_40 s3_46;

for n in r1 s3_4 s3_10 s3_16 s3_22 s3_28 s3_34 s3_40 s3_46; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/${n}
done

for n in r1; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/${n}/db
done

for n in s3_4 s3_10 s3_16 s3_22 s3_28 s3_34 s3_40 s3_46; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/${n}/db
done

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root/xf' r3 s3_6 s3_12 s3_18 s3_24 s3_30 s3_36 s3_42 s3_48;

for n in r3 s3_6 s3_12 s3_18 s3_24 s3_30 s3_36 s3_42 s3_48; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s3_6 s3_12 s3_18 s3_24 s3_30 s3_36 s3_42 s3_48; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root/xf' s3_1 s3_7 s3_13 s3_19 s3_25 s3_31 s3_37 s3_43 s3_49;

for n in s3_1 s3_7 s3_13 s3_19 s3_25 s3_31 s3_37 s3_43 s3_49; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_1 s3_7 s3_13 s3_19 s3_25 s3_31 s3_37 s3_43 s3_49; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root/xf' s3_2 s3_8 s3_14 s3_20 s3_26 s3_32 s3_38 s3_44 s3_50;

for n in s3_2 s3_8 s3_14 s3_20 s3_26 s3_32 s3_38 s3_44 s3_50; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_2 s3_8 s3_14 s3_20 s3_26 s3_32 s3_38 s3_44 s3_50; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root/xf' r2 s3_5 s3_11 s3_17 s3_23 s3_29 s3_35 s3_41 s3_47;

for n in r2 s3_5 s3_11 s3_17 s3_23 s3_29 s3_35 s3_41 s3_47; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s3_5 s3_11 s3_17 s3_23 s3_29 s3_35 s3_41 s3_47; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root/xf' s3_3 s3_9 s3_15 s3_21 s3_27 s3_33 s3_39 s3_45;

for n in s3_3 s3_9 s3_15 s3_21 s3_27 s3_33 s3_39 s3_45; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_3 s3_9 s3_15 s3_21 s3_27 s3_33 s3_39 s3_45; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &

wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/xf/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 xf> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_6 s3_12 s3_18 s3_24 s3_30 s3_36 s3_42 s3_48; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_7 s3_13 s3_19 s3_25 s3_31 s3_37 s3_43 s3_49; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_8 s3_14 s3_20 s3_26 s3_32 s3_38 s3_44 s3_50; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_5 s3_11 s3_17 s3_23 s3_29 s3_35 s3_41 s3_47; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_9 s3_15 s3_21 s3_27 s3_33 s3_39 s3_45; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_4 s3_10 s3_16 s3_22 s3_28 s3_34 s3_40 s3_46; do
cd /root/xf/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node xf> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
