
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=192.168.0.2
server1=192.168.0.3
server2=192.168.0.5
server3=192.168.0.8
server4=192.168.0.7
server5=192.168.0.10
server6=192.168.0.11
server7=192.168.0.6
server8=192.168.0.9
server9=192.168.0.4
target=$1
no_build=$2

echo "==== STEP0: KILL OLDS ===="
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root' r1 s3_8 s3_18 s3_28 s3_38 s3_48
echo "==== 同步中继服务器 ====" 

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root' r2 s3_9 s3_19 s3_29 s3_39 s3_49;

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root' s3_1 s3_11 s3_21 s3_31 s3_41;

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root' s3_4 s3_14 s3_24 s3_34 s3_44;

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root' s3_3 s3_13 s3_23 s3_33 s3_43;

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root' s3_6 s3_16 s3_26 s3_36 s3_46;

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root' s3_7 s3_17 s3_27 s3_37 s3_47;

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root' s3_2 s3_12 s3_22 s3_32 s3_42;

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root' s3_5 s3_15 s3_25 s3_35 s3_45;

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server9} 'Xf4aGbTaf!' '/root' r3 s3_10 s3_20 s3_30 s3_40 s3_50;

EOF
) &

wait

(
echo "[$server0]"
for n in r1 s3_8 s3_18 s3_28 s3_38 s3_48; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/${n}
done
) &


(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
for n in r2 s3_9 s3_19 s3_29 s3_39 s3_49; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db
done

for n in s3_9 s3_19 s3_29 s3_39 s3_49; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
for n in s3_1 s3_11 s3_21 s3_31 s3_41; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_1 s3_11 s3_21 s3_31 s3_41; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
for n in s3_4 s3_14 s3_24 s3_34 s3_44; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_4 s3_14 s3_24 s3_34 s3_44; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
for n in s3_3 s3_13 s3_23 s3_33 s3_43; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_3 s3_13 s3_23 s3_33 s3_43; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
for n in s3_6 s3_16 s3_26 s3_36 s3_46; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_6 s3_16 s3_26 s3_36 s3_46; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
for n in s3_7 s3_17 s3_27 s3_37 s3_47; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_7 s3_17 s3_27 s3_37 s3_47; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
for n in s3_2 s3_12 s3_22 s3_32 s3_42; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_2 s3_12 s3_22 s3_32 s3_42; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
for n in s3_5 s3_15 s3_25 s3_35 s3_45; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_5 s3_15 s3_25 s3_35 s3_45; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
for n in r3 s3_10 s3_20 s3_30 s3_40 s3_50; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db
done

for n in s3_10 s3_20 s3_30 s3_40 s3_50; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &

(

for n in r1; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${n}/db
done

for n in s3_8 s3_18 s3_28 s3_38 s3_48; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${n}/db
done
) &
wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server2]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server3]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server4]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server5]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server6]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server7]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server8]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server9]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 root> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_9 s3_19 s3_29 s3_39 s3_49; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_11 s3_21 s3_31 s3_41; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_14 s3_24 s3_34 s3_44; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_13 s3_23 s3_33 s3_43; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_16 s3_26 s3_36 s3_46; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_17 s3_27 s3_37 s3_47; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_12 s3_22 s3_32 s3_42; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5 s3_15 s3_25 s3_35 s3_45; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_10 s3_20 s3_30 s3_40 s3_50; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_8 s3_18 s3_28 s3_38 s3_48; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node root> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
