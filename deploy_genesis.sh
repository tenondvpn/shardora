
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.201
server1=10.0.0.14
server2=10.0.0.3
server3=10.0.0.40
server4=10.0.0.6
server5=10.0.0.39
server6=10.0.0.5
server7=10.0.0.29
server8=10.0.0.45
server9=10.0.0.49
server10=10.0.0.44
server11=10.0.0.21
server12=10.0.0.27
server13=10.0.0.51
server14=10.0.0.53
server15=10.0.0.57
server16=10.0.0.47
server17=10.0.0.12
server18=10.0.0.50
server19=10.0.0.28
server20=10.0.0.35
server21=10.0.0.15
server22=10.0.0.2
server23=10.0.0.26
server24=10.0.0.8
server25=10.0.0.36
server26=10.0.0.41
server27=10.0.0.42
server28=10.0.0.23
server29=10.0.0.38
server30=10.0.0.48
server31=10.0.0.19
server32=10.0.0.34
server33=10.0.0.54
server34=10.0.0.37
server35=10.0.0.46
server36=10.0.0.32
server37=10.0.0.11
server38=10.0.0.56
server39=10.0.0.43
server40=10.0.0.22
server41=10.0.0.18
server42=10.0.0.33
server43=10.0.0.4
server44=10.0.0.25
server45=10.0.0.16
server46=10.0.0.7
server47=10.0.0.31
server48=10.0.0.9
server49=10.0.0.10
server50=10.0.0.13
server51=10.0.0.20
server52=10.0.0.24
server53=10.0.0.55
server54=10.0.0.58
server55=10.0.0.52
server56=10.0.0.59
server57=10.0.0.30
server58=10.0.0.17
server59=10.0.0.1
target=$1
no_build=$2

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root' r1 s3_58
echo "==== 同步中继服务器 ====" 

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root' s3_12 s3_72;

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root' s3_1 s3_61;

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root' s3_38 s3_98;

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root' s3_4 s3_64;

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root' s3_37 s3_97;

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root' s3_3 s3_63;

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root' s3_27 s3_87;

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root' s3_43;

EOF
) &

wait
echo "==== 同步其他服务器 ====" 

(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server9} 'Xf4aGbTaf!' '/root' s3_47;

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server10} 'Xf4aGbTaf!' '/root' s3_42;

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server11} 'Xf4aGbTaf!' '/root' s3_19 s3_79;

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server12} 'Xf4aGbTaf!' '/root' s3_25 s3_85;

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server13} 'Xf4aGbTaf!' '/root' s3_49;

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server14} 'Xf4aGbTaf!' '/root' s3_51;

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server15} 'Xf4aGbTaf!' '/root' s3_55;

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server16} 'Xf4aGbTaf!' '/root' s3_45;

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server17} 'Xf4aGbTaf!' '/root' s3_10 s3_70;

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server18} 'Xf4aGbTaf!' '/root' s3_48;

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server19} 'Xf4aGbTaf!' '/root' s3_26 s3_86;

EOF
) &


(
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server20} 'Xf4aGbTaf!' '/root' s3_33 s3_93;

EOF
) &


(
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server21} 'Xf4aGbTaf!' '/root' s3_13 s3_73;

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server22} 'Xf4aGbTaf!' '/root' r3 s3_60;

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server23} 'Xf4aGbTaf!' '/root' s3_24 s3_84;

EOF
) &


(
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server24} 'Xf4aGbTaf!' '/root' s3_6 s3_66;

EOF
) &


(
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server25} 'Xf4aGbTaf!' '/root' s3_34 s3_94;

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server26} 'Xf4aGbTaf!' '/root' s3_39 s3_99;

EOF
) &


(
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server27} 'Xf4aGbTaf!' '/root' s3_40 s3_100;

EOF
) &


(
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server28} 'Xf4aGbTaf!' '/root' s3_21 s3_81;

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server29} 'Xf4aGbTaf!' '/root' s3_36 s3_96;

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server30} 'Xf4aGbTaf!' '/root' s3_46;

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server31} 'Xf4aGbTaf!' '/root' s3_17 s3_77;

EOF
) &


(
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server32} 'Xf4aGbTaf!' '/root' s3_32 s3_92;

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server33} 'Xf4aGbTaf!' '/root' s3_52;

EOF
) &


(
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server34} 'Xf4aGbTaf!' '/root' s3_35 s3_95;

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server35} 'Xf4aGbTaf!' '/root' s3_44;

EOF
) &


(
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server36} 'Xf4aGbTaf!' '/root' s3_30 s3_90;

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server37} 'Xf4aGbTaf!' '/root' s3_9 s3_69;

EOF
) &


(
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server38} 'Xf4aGbTaf!' '/root' s3_54;

EOF
) &


(
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server39} 'Xf4aGbTaf!' '/root' s3_41;

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server40} 'Xf4aGbTaf!' '/root' s3_20 s3_80;

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server41} 'Xf4aGbTaf!' '/root' s3_16 s3_76;

EOF
) &


(
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server42} 'Xf4aGbTaf!' '/root' s3_31 s3_91;

EOF
) &


(
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server43} 'Xf4aGbTaf!' '/root' s3_2 s3_62;

EOF
) &


(
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server44} 'Xf4aGbTaf!' '/root' s3_23 s3_83;

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server45} 'Xf4aGbTaf!' '/root' s3_14 s3_74;

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server46} 'Xf4aGbTaf!' '/root' s3_5 s3_65;

EOF
) &


(
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server47} 'Xf4aGbTaf!' '/root' s3_29 s3_89;

EOF
) &


(
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server48} 'Xf4aGbTaf!' '/root' s3_7 s3_67;

EOF
) &


(
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server49} 'Xf4aGbTaf!' '/root' s3_8 s3_68;

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server50} 'Xf4aGbTaf!' '/root' s3_11 s3_71;

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server51} 'Xf4aGbTaf!' '/root' s3_18 s3_78;

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server52} 'Xf4aGbTaf!' '/root' s3_22 s3_82;

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server53} 'Xf4aGbTaf!' '/root' s3_53;

EOF
) &


(
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server54} 'Xf4aGbTaf!' '/root' s3_56;

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server55} 'Xf4aGbTaf!' '/root' s3_50;

EOF
) &


(
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server56} 'Xf4aGbTaf!' '/root' s3_57;

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server57} 'Xf4aGbTaf!' '/root' s3_28 s3_88;

EOF
) &


(
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server58} 'Xf4aGbTaf!' '/root' s3_15 s3_75;

EOF
) &


(
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server59} 'Xf4aGbTaf!' '/root' r2 s3_59;

EOF
) &

wait

(
echo "[$server0]"
for n in r1 s3_58; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/${n}
done
) &


(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
for n in s3_12 s3_72; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_12 s3_72; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
for n in s3_1 s3_61; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_1 s3_61; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
for n in s3_38 s3_98; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_38 s3_98; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
for n in s3_4 s3_64; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_4 s3_64; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
for n in s3_37 s3_97; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_37 s3_97; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
for n in s3_3 s3_63; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_3 s3_63; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
for n in s3_27 s3_87; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_27 s3_87; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
for n in s3_43; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_43; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
for n in s3_47; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_47; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
for n in s3_42; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_42; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
for n in s3_19 s3_79; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_19 s3_79; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
for n in s3_25 s3_85; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_25 s3_85; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
for n in s3_49; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_49; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
for n in s3_51; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_51; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
for n in s3_55; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_55; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
for n in s3_45; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_45; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
for n in s3_10 s3_70; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_10 s3_70; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
for n in s3_48; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_48; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
for n in s3_26 s3_86; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_26 s3_86; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
for n in s3_33 s3_93; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_33 s3_93; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
for n in s3_13 s3_73; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_13 s3_73; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
for n in r3 s3_60; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db
done

for n in s3_60; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
for n in s3_24 s3_84; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_24 s3_84; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
for n in s3_6 s3_66; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_6 s3_66; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
for n in s3_34 s3_94; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_34 s3_94; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
for n in s3_39 s3_99; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_39 s3_99; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
for n in s3_40 s3_100; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_40 s3_100; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
for n in s3_21 s3_81; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_21 s3_81; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
for n in s3_36 s3_96; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_36 s3_96; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
for n in s3_46; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_46; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
for n in s3_17 s3_77; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_17 s3_77; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
for n in s3_32 s3_92; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_32 s3_92; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
for n in s3_52; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_52; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
for n in s3_35 s3_95; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_35 s3_95; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
for n in s3_44; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_44; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
for n in s3_30 s3_90; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_30 s3_90; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
for n in s3_9 s3_69; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_9 s3_69; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
for n in s3_54; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_54; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
for n in s3_41; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_41; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
for n in s3_20 s3_80; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_20 s3_80; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
for n in s3_16 s3_76; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_16 s3_76; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
for n in s3_31 s3_91; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_31 s3_91; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
for n in s3_2 s3_62; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_2 s3_62; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
for n in s3_23 s3_83; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_23 s3_83; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
for n in s3_14 s3_74; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_14 s3_74; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
for n in s3_5 s3_65; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_5 s3_65; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
for n in s3_29 s3_89; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_29 s3_89; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
for n in s3_7 s3_67; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_7 s3_67; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
for n in s3_8 s3_68; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_8 s3_68; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
for n in s3_11 s3_71; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_11 s3_71; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
for n in s3_18 s3_78; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_18 s3_78; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
for n in s3_22 s3_82; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_22 s3_82; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
for n in s3_53; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_53; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
for n in s3_56; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_56; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
for n in s3_50; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_50; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
for n in s3_57; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_57; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
for n in s3_28 s3_88; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_28 s3_88; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
for n in s3_15 s3_75; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_15 s3_75; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
for n in r2 s3_59; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db
done

for n in s3_59; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db
done

EOF
) &

(

for n in r1; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${n}/db
done

for n in s3_58; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${n}/db
done
) &
wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

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

echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 root> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_12 s3_72; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_61; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_38 s3_98; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_64; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_37 s3_97; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_63; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_27 s3_87; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_43; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_47; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_42; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_19 s3_79; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_25 s3_85; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_49; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_51; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_55; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_45; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10 s3_70; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_48; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_26 s3_86; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server20 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_33 s3_93; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server21 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_13 s3_73; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server22 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_60; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server23 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_24 s3_84; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server24 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_66; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server25 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_34 s3_94; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server26 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_39 s3_99; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server27 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_40 s3_100; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server28 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_21 s3_81; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server29 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_36 s3_96; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server30 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_46; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server31 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_17 s3_77; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server32 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_32 s3_92; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server33 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_52; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server34 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_35 s3_95; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server35 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_44; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server36 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_30 s3_90; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server37 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_9 s3_69; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server38 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_54; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server39 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_41; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server40 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_20 s3_80; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server41 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_16 s3_76; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server42 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_31 s3_91; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server43 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_62; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server44 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_23 s3_83; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server45 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_14 s3_74; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server46 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5 s3_65; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server47 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_29 s3_89; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server48 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_67; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server49 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8 s3_68; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server50 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_11 s3_71; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server51 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_18 s3_78; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server52 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_22 s3_82; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server53 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_53; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server54 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_56; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server55 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_50; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server56 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_57; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server57 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_28 s3_88; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server58 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_15 s3_75; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server59 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_59; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_58; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node root> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
