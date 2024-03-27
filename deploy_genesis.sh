
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.16
server1=10.0.0.33
server2=10.0.0.18
server3=10.0.0.24
server4=10.0.0.30
server5=10.0.0.26
server6=10.0.0.21
server7=10.0.0.22
server8=10.0.0.31
server9=10.0.0.23
server10=10.0.0.29
server11=10.0.0.19
server12=10.0.0.28
server13=10.0.0.35
server14=10.0.0.20
server15=10.0.0.27
server16=10.0.0.32
server17=10.0.0.34
server18=10.0.0.25
server19=10.0.0.17
target=$1
no_build=$2

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root/xf && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root/xf' r1 s3_1;

for n in r1 s3_1; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/${n}
done

for n in r1; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/${n}/db
done

for n in s3_1; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/${n}/db
done

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root/xf' s4_5;

for n in s4_5; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_5; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root/xf' r3 s4_10;

for n in r3 s4_10; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s4_10; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root/xf' s3_6;

for n in s3_6; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_6; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root/xf' s4_2;

for n in s4_2; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_2; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root/xf' s3_8;

for n in s3_8; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_8; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root/xf' s3_3;

for n in s3_3; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_3; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root/xf' s3_4;

for n in s3_4; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_4; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root/xf' s4_3;

for n in s4_3; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_3; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server9} 'Xf4aGbTaf!' '/root/xf' s3_5;

for n in s3_5; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_5; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server10} 'Xf4aGbTaf!' '/root/xf' s4_1;

for n in s4_1; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_1; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server11} 'Xf4aGbTaf!' '/root/xf' s4_8;

for n in s4_8; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_8; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server12} 'Xf4aGbTaf!' '/root/xf' s3_10;

for n in s3_10; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_10; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server13} 'Xf4aGbTaf!' '/root/xf' s4_7;

for n in s4_7; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_7; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server14} 'Xf4aGbTaf!' '/root/xf' s3_2;

for n in s3_2; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_2; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server15} 'Xf4aGbTaf!' '/root/xf' s3_9;

for n in s3_9; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_9; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server16} 'Xf4aGbTaf!' '/root/xf' s4_4;

for n in s4_4; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_4; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server17} 'Xf4aGbTaf!' '/root/xf' s4_6;

for n in s4_6; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s4_6; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server18} 'Xf4aGbTaf!' '/root/xf' s3_7;

for n in s3_7; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_7; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server19} 'Xf4aGbTaf!' '/root/xf' r2 s4_9;

for n in r2 s4_9; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s4_9; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
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

echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
ps -ef | grep zjchain | grep xf | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
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
for node in s4_5; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s4_10; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_2; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_3; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_1; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_8; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_7; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_9; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_4; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s4_6; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s4_9; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_1; do
cd /root/xf/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node xf> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
