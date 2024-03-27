
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.16
server1=10.0.0.29
server2=10.0.0.21
server3=10.0.0.33
server4=10.0.0.19
server5=10.0.0.32
server6=10.0.0.18
server7=10.0.0.20
server8=10.0.0.23
server9=10.0.0.26
server10=10.0.0.17
server11=10.0.0.30
server12=10.0.0.25
server13=10.0.0.22
server14=10.0.0.28
server15=10.0.0.34
server16=10.0.0.31
server17=10.0.0.35
server18=10.0.0.27
server19=10.0.0.24
target=$1
no_build=$2

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root/xf && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root/xf' r1 s3_18 s3_38 s4_8 s4_28 s4_48;

for n in r1 s3_18 s3_38 s4_8 s4_28 s4_48; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/${n}
done

for n in r1; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/${n}/db
done

for n in s3_18 s3_38; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/${n}/db
done

for n in s4_8 s4_28 s4_48; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/${n}/db
done

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root/xf' s3_11 s3_31 s4_1 s4_21 s4_41;

for n in s3_11 s3_31 s4_1 s4_21 s4_41; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_11 s3_31; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_1 s4_21 s4_41; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root/xf' s3_3 s3_23 s3_43 s4_13 s4_33;

for n in s3_3 s3_23 s3_43 s4_13 s4_33; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_3 s3_23 s3_43; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_13 s4_33; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root/xf' s3_15 s3_35 s4_5 s4_25 s4_45;

for n in s3_15 s3_35 s4_5 s4_25 s4_45; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_15 s3_35; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_5 s4_25 s4_45; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root/xf' s3_1 s3_21 s3_41 s4_11 s4_31;

for n in s3_1 s3_21 s3_41 s4_11 s4_31; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_1 s3_21 s3_41; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_11 s4_31; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root/xf' s3_14 s3_34 s4_4 s4_24 s4_44;

for n in s3_14 s3_34 s4_4 s4_24 s4_44; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_14 s3_34; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_4 s4_24 s4_44; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root/xf' r3 s3_20 s3_40 s4_10 s4_30 s4_50;

for n in r3 s3_20 s3_40 s4_10 s4_30 s4_50; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s3_20 s3_40; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_10 s4_30 s4_50; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root/xf' s3_2 s3_22 s3_42 s4_12 s4_32;

for n in s3_2 s3_22 s3_42 s4_12 s4_32; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_2 s3_22 s3_42; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_12 s4_32; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root/xf' s3_5 s3_25 s3_45 s4_15 s4_35;

for n in s3_5 s3_25 s3_45 s4_15 s4_35; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_5 s3_25 s3_45; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_15 s4_35; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server9} 'Xf4aGbTaf!' '/root/xf' s3_8 s3_28 s3_48 s4_18 s4_38;

for n in s3_8 s3_28 s3_48 s4_18 s4_38; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_8 s3_28 s3_48; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_18 s4_38; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server10} 'Xf4aGbTaf!' '/root/xf' r2 s3_19 s3_39 s4_9 s4_29 s4_49;

for n in r2 s3_19 s3_39 s4_9 s4_29 s4_49; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s3_19 s3_39; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_9 s4_29 s4_49; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server11} 'Xf4aGbTaf!' '/root/xf' s3_12 s3_32 s4_2 s4_22 s4_42;

for n in s3_12 s3_32 s4_2 s4_22 s4_42; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_12 s3_32; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_2 s4_22 s4_42; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server12} 'Xf4aGbTaf!' '/root/xf' s3_7 s3_27 s3_47 s4_17 s4_37;

for n in s3_7 s3_27 s3_47 s4_17 s4_37; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_7 s3_27 s3_47; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_17 s4_37; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server13} 'Xf4aGbTaf!' '/root/xf' s3_4 s3_24 s3_44 s4_14 s4_34;

for n in s3_4 s3_24 s3_44 s4_14 s4_34; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_4 s3_24 s3_44; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_14 s4_34; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server14} 'Xf4aGbTaf!' '/root/xf' s3_10 s3_30 s3_50 s4_20 s4_40;

for n in s3_10 s3_30 s3_50 s4_20 s4_40; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_10 s3_30 s3_50; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_20 s4_40; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server15} 'Xf4aGbTaf!' '/root/xf' s3_16 s3_36 s4_6 s4_26 s4_46;

for n in s3_16 s3_36 s4_6 s4_26 s4_46; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_16 s3_36; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_6 s4_26 s4_46; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server16} 'Xf4aGbTaf!' '/root/xf' s3_13 s3_33 s4_3 s4_23 s4_43;

for n in s3_13 s3_33 s4_3 s4_23 s4_43; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_13 s3_33; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_3 s4_23 s4_43; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server17} 'Xf4aGbTaf!' '/root/xf' s3_17 s3_37 s4_7 s4_27 s4_47;

for n in s3_17 s3_37 s4_7 s4_27 s4_47; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_17 s3_37; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_7 s4_27 s4_47; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server18} 'Xf4aGbTaf!' '/root/xf' s3_9 s3_29 s3_49 s4_19 s4_39;

for n in s3_9 s3_29 s3_49 s4_19 s4_39; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_9 s3_29 s3_49; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_19 s4_39; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_4 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server19} 'Xf4aGbTaf!' '/root/xf' s3_6 s3_26 s3_46 s4_16 s4_36;

for n in s3_6 s3_26 s3_46 s4_16 s4_36; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_6 s3_26 s3_46; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

for n in s4_16 s4_36; do
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
for node in s3_11 s3_31 s4_1 s4_21 s4_41; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_23 s3_43 s4_13 s4_33; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_15 s3_35 s4_5 s4_25 s4_45; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_21 s3_41 s4_11 s4_31; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_14 s3_34 s4_4 s4_24 s4_44; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_20 s3_40 s4_10 s4_30 s4_50; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_22 s3_42 s4_12 s4_32; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5 s3_25 s3_45 s4_15 s4_35; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8 s3_28 s3_48 s4_18 s4_38; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_19 s3_39 s4_9 s4_29 s4_49; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_12 s3_32 s4_2 s4_22 s4_42; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_27 s3_47 s4_17 s4_37; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_24 s3_44 s4_14 s4_34; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10 s3_30 s3_50 s4_20 s4_40; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_16 s3_36 s4_6 s4_26 s4_46; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_13 s3_33 s4_3 s4_23 s4_43; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_17 s3_37 s4_7 s4_27 s4_47; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_9 s3_29 s3_49 s4_19 s4_39; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_26 s3_46 s4_16 s4_36; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_18 s3_38 s4_8 s4_28 s4_48; do
cd /root/xf/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node xf> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
