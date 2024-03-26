
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.16
server1=10.0.0.27
server2=10.0.0.23
server3=10.0.0.35
server4=10.0.0.24
server5=10.0.0.30
server6=10.0.0.22
server7=10.0.0.25
server8=10.0.0.29
server9=10.0.0.18
server10=10.0.0.33
server11=10.0.0.31
server12=10.0.0.19
server13=10.0.0.28
server14=10.0.0.34
server15=10.0.0.21
server16=10.0.0.20
server17=10.0.0.32
server18=10.0.0.26
server19=10.0.0.17
target=$1
no_build=$2

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root/xf && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root/xf' r1 s3_18 s3_38 s3_58 s3_78 s3_98;

for n in r1 s3_18 s3_38 s3_58 s3_78 s3_98; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/${n}
done

for n in r1; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/${n}/db
done

for n in s3_18 s3_38 s3_58 s3_78 s3_98; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/${n}/db
done

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root/xf' s3_9 s3_29 s3_49 s3_69 s3_89;

for n in s3_9 s3_29 s3_49 s3_69 s3_89; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_9 s3_29 s3_49 s3_69 s3_89; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root/xf' s3_5 s3_25 s3_45 s3_65 s3_85;

for n in s3_5 s3_25 s3_45 s3_65 s3_85; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_5 s3_25 s3_45 s3_65 s3_85; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root/xf' s3_17 s3_37 s3_57 s3_77 s3_97;

for n in s3_17 s3_37 s3_57 s3_77 s3_97; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_17 s3_37 s3_57 s3_77 s3_97; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root/xf' s3_6 s3_26 s3_46 s3_66 s3_86;

for n in s3_6 s3_26 s3_46 s3_66 s3_86; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_6 s3_26 s3_46 s3_66 s3_86; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root/xf' s3_12 s3_32 s3_52 s3_72 s3_92;

for n in s3_12 s3_32 s3_52 s3_72 s3_92; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_12 s3_32 s3_52 s3_72 s3_92; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root/xf' s3_4 s3_24 s3_44 s3_64 s3_84;

for n in s3_4 s3_24 s3_44 s3_64 s3_84; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_4 s3_24 s3_44 s3_64 s3_84; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root/xf' s3_7 s3_27 s3_47 s3_67 s3_87;

for n in s3_7 s3_27 s3_47 s3_67 s3_87; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_7 s3_27 s3_47 s3_67 s3_87; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root/xf' s3_11 s3_31 s3_51 s3_71 s3_91;

for n in s3_11 s3_31 s3_51 s3_71 s3_91; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_11 s3_31 s3_51 s3_71 s3_91; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server9} 'Xf4aGbTaf!' '/root/xf' r3 s3_20 s3_40 s3_60 s3_80 s3_100;

for n in r3 s3_20 s3_40 s3_60 s3_80 s3_100; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s3_20 s3_40 s3_60 s3_80 s3_100; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server10} 'Xf4aGbTaf!' '/root/xf' s3_15 s3_35 s3_55 s3_75 s3_95;

for n in s3_15 s3_35 s3_55 s3_75 s3_95; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_15 s3_35 s3_55 s3_75 s3_95; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server11} 'Xf4aGbTaf!' '/root/xf' s3_13 s3_33 s3_53 s3_73 s3_93;

for n in s3_13 s3_33 s3_53 s3_73 s3_93; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_13 s3_33 s3_53 s3_73 s3_93; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server12} 'Xf4aGbTaf!' '/root/xf' s3_1 s3_21 s3_41 s3_61 s3_81;

for n in s3_1 s3_21 s3_41 s3_61 s3_81; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_1 s3_21 s3_41 s3_61 s3_81; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server13} 'Xf4aGbTaf!' '/root/xf' s3_10 s3_30 s3_50 s3_70 s3_90;

for n in s3_10 s3_30 s3_50 s3_70 s3_90; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_10 s3_30 s3_50 s3_70 s3_90; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server14} 'Xf4aGbTaf!' '/root/xf' s3_16 s3_36 s3_56 s3_76 s3_96;

for n in s3_16 s3_36 s3_56 s3_76 s3_96; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_16 s3_36 s3_56 s3_76 s3_96; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server15} 'Xf4aGbTaf!' '/root/xf' s3_3 s3_23 s3_43 s3_63 s3_83;

for n in s3_3 s3_23 s3_43 s3_63 s3_83; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_3 s3_23 s3_43 s3_63 s3_83; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server16} 'Xf4aGbTaf!' '/root/xf' s3_2 s3_22 s3_42 s3_62 s3_82;

for n in s3_2 s3_22 s3_42 s3_62 s3_82; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_2 s3_22 s3_42 s3_62 s3_82; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server17} 'Xf4aGbTaf!' '/root/xf' s3_14 s3_34 s3_54 s3_74 s3_94;

for n in s3_14 s3_34 s3_54 s3_74 s3_94; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_14 s3_34 s3_54 s3_74 s3_94; do
    cp -rf /root/xf/zjnodes/zjchain/shard_db_3 /root/xf/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
mkdir -p /root/xf;
rm -rf /root/xf/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/xf/fetch.sh /root/xf/
cd /root/xf && sh -x fetch.sh ${server0} ${server18} 'Xf4aGbTaf!' '/root/xf' s3_8 s3_28 s3_48 s3_68 s3_88;

for n in s3_8 s3_28 s3_48 s3_68 s3_88; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in s3_8 s3_28 s3_48 s3_68 s3_88; do
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
cd /root/xf && sh -x fetch.sh ${server0} ${server19} 'Xf4aGbTaf!' '/root/xf' r2 s3_19 s3_39 s3_59 s3_79 s3_99;

for n in r2 s3_19 s3_39 s3_59 s3_79 s3_99; do
    cp -rf /root/xf/zjnodes/zjchain/GeoLite2-City.mmdb /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/conf/log4cpp.properties /root/xf/zjnodes/\${n}/conf
    cp -rf /root/xf/zjnodes/zjchain/zjchain /root/xf/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/xf/zjnodes/zjchain/root_db /root/xf/zjnodes/\${n}/db
done

for n in s3_19 s3_39 s3_59 s3_79 s3_99; do
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
for node in s3_9 s3_29 s3_49 s3_69 s3_89; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5 s3_25 s3_45 s3_65 s3_85; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_17 s3_37 s3_57 s3_77 s3_97; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_26 s3_46 s3_66 s3_86; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_12 s3_32 s3_52 s3_72 s3_92; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_24 s3_44 s3_64 s3_84; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_27 s3_47 s3_67 s3_87; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_11 s3_31 s3_51 s3_71 s3_91; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_20 s3_40 s3_60 s3_80 s3_100; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_15 s3_35 s3_55 s3_75 s3_95; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_13 s3_33 s3_53 s3_73 s3_93; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_21 s3_41 s3_61 s3_81; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10 s3_30 s3_50 s3_70 s3_90; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_16 s3_36 s3_56 s3_76 s3_96; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_23 s3_43 s3_63 s3_83; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_22 s3_42 s3_62 s3_82; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_14 s3_34 s3_54 s3_74 s3_94; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8 s3_28 s3_48 s3_68 s3_88; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_19 s3_39 s3_59 s3_79 s3_99; do \
    cd /root/xf/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node xf> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_18 s3_38 s3_58 s3_78 s3_98; do
cd /root/xf/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node xf> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
