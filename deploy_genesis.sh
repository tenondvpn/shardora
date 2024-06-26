
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.201
server1=10.0.0.7
server2=10.0.0.4
server3=10.0.0.53
server4=10.0.0.2
server5=10.0.0.33
server6=10.0.0.29
server7=10.0.0.9
server8=10.0.0.27
server9=10.0.0.8
server10=10.0.0.56
server11=10.0.0.43
server12=10.0.0.35
server13=10.0.0.45
server14=10.0.0.46
server15=10.0.0.48
server16=10.0.0.15
server17=10.0.0.24
server18=10.0.0.22
server19=10.0.0.59
server20=10.0.0.42
server21=10.0.0.31
server22=10.0.0.39
server23=10.0.0.16
server24=10.0.0.1
server25=10.0.0.52
server26=10.0.0.57
server27=10.0.0.17
server28=10.0.0.3
server29=10.0.0.37
server30=10.0.0.12
server31=10.0.0.26
server32=10.0.0.6
server33=10.0.0.47
server34=10.0.0.36
server35=10.0.0.34
server36=10.0.0.32
server37=10.0.0.20
server38=10.0.0.14
server39=10.0.0.21
server40=10.0.0.38
server41=10.0.0.10
server42=10.0.0.51
server43=10.0.0.50
server44=10.0.0.19
server45=10.0.0.18
server46=10.0.0.28
server47=10.0.0.55
server48=10.0.0.41
server49=10.0.0.40
server50=10.0.0.23
server51=10.0.0.11
server52=10.0.0.5
server53=10.0.0.54
server54=10.0.0.25
server55=10.0.0.30
server56=10.0.0.13
server57=10.0.0.58
server58=10.0.0.44
server59=10.0.0.49
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

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root' r1 s3_58 s3_118 s3_178 s3_238 s3_298
echo "==== 同步中继服务器 ====" 

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root' s3_5 s3_65 s3_125 s3_185 s3_245;

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root' s3_2 s3_62 s3_122 s3_182 s3_242;

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root' s3_51 s3_111 s3_171 s3_231 s3_291;

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root' r3 s3_60 s3_120 s3_180 s3_240 s3_300;

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root' s3_31 s3_91 s3_151 s3_211 s3_271;

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root' s3_27 s3_87 s3_147 s3_207 s3_267;

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root' s3_7 s3_67 s3_127 s3_187 s3_247;

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root' s3_25 s3_85 s3_145 s3_205 s3_265;

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
cd /root && sh -x fetch.sh ${server2} ${server9} 'Xf4aGbTaf!' '/root' s3_6 s3_66 s3_126 s3_186 s3_246;

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server10} 'Xf4aGbTaf!' '/root' s3_54 s3_114 s3_174 s3_234 s3_294;

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server11} 'Xf4aGbTaf!' '/root' s3_41 s3_101 s3_161 s3_221 s3_281;

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server12} 'Xf4aGbTaf!' '/root' s3_33 s3_93 s3_153 s3_213 s3_273;

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server13} 'Xf4aGbTaf!' '/root' s3_43 s3_103 s3_163 s3_223 s3_283;

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server14} 'Xf4aGbTaf!' '/root' s3_44 s3_104 s3_164 s3_224 s3_284;

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server15} 'Xf4aGbTaf!' '/root' s3_46 s3_106 s3_166 s3_226 s3_286;

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server16} 'Xf4aGbTaf!' '/root' s3_13 s3_73 s3_133 s3_193 s3_253;

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server17} 'Xf4aGbTaf!' '/root' s3_22 s3_82 s3_142 s3_202 s3_262;

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server18} 'Xf4aGbTaf!' '/root' s3_20 s3_80 s3_140 s3_200 s3_260;

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server19} 'Xf4aGbTaf!' '/root' s3_57 s3_117 s3_177 s3_237 s3_297;

EOF
) &


(
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server20} 'Xf4aGbTaf!' '/root' s3_40 s3_100 s3_160 s3_220 s3_280;

EOF
) &


(
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server21} 'Xf4aGbTaf!' '/root' s3_29 s3_89 s3_149 s3_209 s3_269;

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server22} 'Xf4aGbTaf!' '/root' s3_37 s3_97 s3_157 s3_217 s3_277;

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server23} 'Xf4aGbTaf!' '/root' s3_14 s3_74 s3_134 s3_194 s3_254;

EOF
) &


(
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server24} 'Xf4aGbTaf!' '/root' r2 s3_59 s3_119 s3_179 s3_239 s3_299;

EOF
) &


(
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server25} 'Xf4aGbTaf!' '/root' s3_50 s3_110 s3_170 s3_230 s3_290;

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server26} 'Xf4aGbTaf!' '/root' s3_55 s3_115 s3_175 s3_235 s3_295;

EOF
) &


(
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server27} 'Xf4aGbTaf!' '/root' s3_15 s3_75 s3_135 s3_195 s3_255;

EOF
) &


(
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server28} 'Xf4aGbTaf!' '/root' s3_1 s3_61 s3_121 s3_181 s3_241;

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server29} 'Xf4aGbTaf!' '/root' s3_35 s3_95 s3_155 s3_215 s3_275;

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server30} 'Xf4aGbTaf!' '/root' s3_10 s3_70 s3_130 s3_190 s3_250;

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server31} 'Xf4aGbTaf!' '/root' s3_24 s3_84 s3_144 s3_204 s3_264;

EOF
) &


(
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server32} 'Xf4aGbTaf!' '/root' s3_4 s3_64 s3_124 s3_184 s3_244;

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server33} 'Xf4aGbTaf!' '/root' s3_45 s3_105 s3_165 s3_225 s3_285;

EOF
) &


(
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server34} 'Xf4aGbTaf!' '/root' s3_34 s3_94 s3_154 s3_214 s3_274;

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server35} 'Xf4aGbTaf!' '/root' s3_32 s3_92 s3_152 s3_212 s3_272;

EOF
) &


(
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server36} 'Xf4aGbTaf!' '/root' s3_30 s3_90 s3_150 s3_210 s3_270;

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server37} 'Xf4aGbTaf!' '/root' s3_18 s3_78 s3_138 s3_198 s3_258;

EOF
) &


(
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server38} 'Xf4aGbTaf!' '/root' s3_12 s3_72 s3_132 s3_192 s3_252;

EOF
) &


(
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server39} 'Xf4aGbTaf!' '/root' s3_19 s3_79 s3_139 s3_199 s3_259;

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server40} 'Xf4aGbTaf!' '/root' s3_36 s3_96 s3_156 s3_216 s3_276;

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server41} 'Xf4aGbTaf!' '/root' s3_8 s3_68 s3_128 s3_188 s3_248;

EOF
) &


(
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server42} 'Xf4aGbTaf!' '/root' s3_49 s3_109 s3_169 s3_229 s3_289;

EOF
) &


(
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server43} 'Xf4aGbTaf!' '/root' s3_48 s3_108 s3_168 s3_228 s3_288;

EOF
) &


(
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server44} 'Xf4aGbTaf!' '/root' s3_17 s3_77 s3_137 s3_197 s3_257;

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server45} 'Xf4aGbTaf!' '/root' s3_16 s3_76 s3_136 s3_196 s3_256;

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server46} 'Xf4aGbTaf!' '/root' s3_26 s3_86 s3_146 s3_206 s3_266;

EOF
) &


(
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server47} 'Xf4aGbTaf!' '/root' s3_53 s3_113 s3_173 s3_233 s3_293;

EOF
) &


(
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server48} 'Xf4aGbTaf!' '/root' s3_39 s3_99 s3_159 s3_219 s3_279;

EOF
) &


(
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server49} 'Xf4aGbTaf!' '/root' s3_38 s3_98 s3_158 s3_218 s3_278;

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server50} 'Xf4aGbTaf!' '/root' s3_21 s3_81 s3_141 s3_201 s3_261;

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server51} 'Xf4aGbTaf!' '/root' s3_9 s3_69 s3_129 s3_189 s3_249;

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server52} 'Xf4aGbTaf!' '/root' s3_3 s3_63 s3_123 s3_183 s3_243;

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server53} 'Xf4aGbTaf!' '/root' s3_52 s3_112 s3_172 s3_232 s3_292;

EOF
) &


(
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server54} 'Xf4aGbTaf!' '/root' s3_23 s3_83 s3_143 s3_203 s3_263;

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server55} 'Xf4aGbTaf!' '/root' s3_28 s3_88 s3_148 s3_208 s3_268;

EOF
) &


(
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server56} 'Xf4aGbTaf!' '/root' s3_11 s3_71 s3_131 s3_191 s3_251;

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server57} 'Xf4aGbTaf!' '/root' s3_56 s3_116 s3_176 s3_236 s3_296;

EOF
) &


(
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server58} 'Xf4aGbTaf!' '/root' s3_42 s3_102 s3_162 s3_222 s3_282;

EOF
) &


(
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server59} 'Xf4aGbTaf!' '/root' s3_47 s3_107 s3_167 s3_227 s3_287;

EOF
) &

wait

(
echo "[$server0]"
for n in r1 s3_58 s3_118 s3_178 s3_238 s3_298; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/${n}
done
) &


(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
for n in s3_5 s3_65 s3_125 s3_185 s3_245; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_5 s3_65 s3_125 s3_185 s3_245; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
for n in s3_2 s3_62 s3_122 s3_182 s3_242; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_2 s3_62 s3_122 s3_182 s3_242; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
for n in s3_51 s3_111 s3_171 s3_231 s3_291; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_51 s3_111 s3_171 s3_231 s3_291; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
for n in r3 s3_60 s3_120 s3_180 s3_240 s3_300; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db &
done
wait

for n in s3_60 s3_120 s3_180 s3_240 s3_300; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
for n in s3_31 s3_91 s3_151 s3_211 s3_271; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_31 s3_91 s3_151 s3_211 s3_271; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
for n in s3_27 s3_87 s3_147 s3_207 s3_267; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_27 s3_87 s3_147 s3_207 s3_267; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
for n in s3_7 s3_67 s3_127 s3_187 s3_247; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_7 s3_67 s3_127 s3_187 s3_247; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
for n in s3_25 s3_85 s3_145 s3_205 s3_265; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_25 s3_85 s3_145 s3_205 s3_265; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
for n in s3_6 s3_66 s3_126 s3_186 s3_246; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_6 s3_66 s3_126 s3_186 s3_246; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
for n in s3_54 s3_114 s3_174 s3_234 s3_294; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_54 s3_114 s3_174 s3_234 s3_294; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
for n in s3_41 s3_101 s3_161 s3_221 s3_281; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_41 s3_101 s3_161 s3_221 s3_281; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
for n in s3_33 s3_93 s3_153 s3_213 s3_273; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_33 s3_93 s3_153 s3_213 s3_273; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
for n in s3_43 s3_103 s3_163 s3_223 s3_283; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_43 s3_103 s3_163 s3_223 s3_283; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
for n in s3_44 s3_104 s3_164 s3_224 s3_284; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_44 s3_104 s3_164 s3_224 s3_284; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
for n in s3_46 s3_106 s3_166 s3_226 s3_286; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_46 s3_106 s3_166 s3_226 s3_286; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
for n in s3_13 s3_73 s3_133 s3_193 s3_253; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_13 s3_73 s3_133 s3_193 s3_253; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
for n in s3_22 s3_82 s3_142 s3_202 s3_262; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_22 s3_82 s3_142 s3_202 s3_262; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
for n in s3_20 s3_80 s3_140 s3_200 s3_260; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_20 s3_80 s3_140 s3_200 s3_260; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
for n in s3_57 s3_117 s3_177 s3_237 s3_297; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_57 s3_117 s3_177 s3_237 s3_297; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
for n in s3_40 s3_100 s3_160 s3_220 s3_280; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_40 s3_100 s3_160 s3_220 s3_280; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
for n in s3_29 s3_89 s3_149 s3_209 s3_269; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_29 s3_89 s3_149 s3_209 s3_269; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
for n in s3_37 s3_97 s3_157 s3_217 s3_277; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_37 s3_97 s3_157 s3_217 s3_277; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
for n in s3_14 s3_74 s3_134 s3_194 s3_254; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_14 s3_74 s3_134 s3_194 s3_254; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
for n in r2 s3_59 s3_119 s3_179 s3_239 s3_299; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db &
done
wait

for n in s3_59 s3_119 s3_179 s3_239 s3_299; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
for n in s3_50 s3_110 s3_170 s3_230 s3_290; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_50 s3_110 s3_170 s3_230 s3_290; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
for n in s3_55 s3_115 s3_175 s3_235 s3_295; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_55 s3_115 s3_175 s3_235 s3_295; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
for n in s3_15 s3_75 s3_135 s3_195 s3_255; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_15 s3_75 s3_135 s3_195 s3_255; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
for n in s3_1 s3_61 s3_121 s3_181 s3_241; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_1 s3_61 s3_121 s3_181 s3_241; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
for n in s3_35 s3_95 s3_155 s3_215 s3_275; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_35 s3_95 s3_155 s3_215 s3_275; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
for n in s3_10 s3_70 s3_130 s3_190 s3_250; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_10 s3_70 s3_130 s3_190 s3_250; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
for n in s3_24 s3_84 s3_144 s3_204 s3_264; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_24 s3_84 s3_144 s3_204 s3_264; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
for n in s3_4 s3_64 s3_124 s3_184 s3_244; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_4 s3_64 s3_124 s3_184 s3_244; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
for n in s3_45 s3_105 s3_165 s3_225 s3_285; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_45 s3_105 s3_165 s3_225 s3_285; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
for n in s3_34 s3_94 s3_154 s3_214 s3_274; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_34 s3_94 s3_154 s3_214 s3_274; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
for n in s3_32 s3_92 s3_152 s3_212 s3_272; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_32 s3_92 s3_152 s3_212 s3_272; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
for n in s3_30 s3_90 s3_150 s3_210 s3_270; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_30 s3_90 s3_150 s3_210 s3_270; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
for n in s3_18 s3_78 s3_138 s3_198 s3_258; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_18 s3_78 s3_138 s3_198 s3_258; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
for n in s3_12 s3_72 s3_132 s3_192 s3_252; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_12 s3_72 s3_132 s3_192 s3_252; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
for n in s3_19 s3_79 s3_139 s3_199 s3_259; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_19 s3_79 s3_139 s3_199 s3_259; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
for n in s3_36 s3_96 s3_156 s3_216 s3_276; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_36 s3_96 s3_156 s3_216 s3_276; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
for n in s3_8 s3_68 s3_128 s3_188 s3_248; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_8 s3_68 s3_128 s3_188 s3_248; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
for n in s3_49 s3_109 s3_169 s3_229 s3_289; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_49 s3_109 s3_169 s3_229 s3_289; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
for n in s3_48 s3_108 s3_168 s3_228 s3_288; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_48 s3_108 s3_168 s3_228 s3_288; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
for n in s3_17 s3_77 s3_137 s3_197 s3_257; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_17 s3_77 s3_137 s3_197 s3_257; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
for n in s3_16 s3_76 s3_136 s3_196 s3_256; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_16 s3_76 s3_136 s3_196 s3_256; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
for n in s3_26 s3_86 s3_146 s3_206 s3_266; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_26 s3_86 s3_146 s3_206 s3_266; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
for n in s3_53 s3_113 s3_173 s3_233 s3_293; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_53 s3_113 s3_173 s3_233 s3_293; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
for n in s3_39 s3_99 s3_159 s3_219 s3_279; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_39 s3_99 s3_159 s3_219 s3_279; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
for n in s3_38 s3_98 s3_158 s3_218 s3_278; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_38 s3_98 s3_158 s3_218 s3_278; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
for n in s3_21 s3_81 s3_141 s3_201 s3_261; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_21 s3_81 s3_141 s3_201 s3_261; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
for n in s3_9 s3_69 s3_129 s3_189 s3_249; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_9 s3_69 s3_129 s3_189 s3_249; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
for n in s3_3 s3_63 s3_123 s3_183 s3_243; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_3 s3_63 s3_123 s3_183 s3_243; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
for n in s3_52 s3_112 s3_172 s3_232 s3_292; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_52 s3_112 s3_172 s3_232 s3_292; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
for n in s3_23 s3_83 s3_143 s3_203 s3_263; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_23 s3_83 s3_143 s3_203 s3_263; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
for n in s3_28 s3_88 s3_148 s3_208 s3_268; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_28 s3_88 s3_148 s3_208 s3_268; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
for n in s3_11 s3_71 s3_131 s3_191 s3_251; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_11 s3_71 s3_131 s3_191 s3_251; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
for n in s3_56 s3_116 s3_176 s3_236 s3_296; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_56 s3_116 s3_176 s3_236 s3_296; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
for n in s3_42 s3_102 s3_162 s3_222 s3_282; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_42 s3_102 s3_162 s3_222 s3_282; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
for n in s3_47 s3_107 s3_167 s3_227 s3_287; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_47 s3_107 s3_167 s3_227 s3_287; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &

(

for n in r1; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${n}/db &
done
wait

for n in s3_58 s3_118 s3_178 s3_238 s3_298; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/${n}/db &
done
wait
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

echo "[$server10]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server11]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server12]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server13]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server14]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server15]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server16]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server17]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server18]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server19]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server20]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server21]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server22]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server23]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server24]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server25]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server26]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server27]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server28]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server29]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server30]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server31]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server32]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server33]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server34]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server35]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server36]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server37]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server38]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server39]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server40]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server41]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server42]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server43]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server44]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server45]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server46]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server47]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server48]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server49]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server50]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server51]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server52]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server53]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server54]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server55]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server56]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server57]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server58]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server59]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<"EOF"
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
for node in s3_5 s3_65 s3_125 s3_185 s3_245; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_62 s3_122 s3_182 s3_242; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_51 s3_111 s3_171 s3_231 s3_291; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_60 s3_120 s3_180 s3_240 s3_300; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_31 s3_91 s3_151 s3_211 s3_271; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_27 s3_87 s3_147 s3_207 s3_267; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_67 s3_127 s3_187 s3_247; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_25 s3_85 s3_145 s3_205 s3_265; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_66 s3_126 s3_186 s3_246; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_54 s3_114 s3_174 s3_234 s3_294; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_41 s3_101 s3_161 s3_221 s3_281; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_33 s3_93 s3_153 s3_213 s3_273; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_43 s3_103 s3_163 s3_223 s3_283; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_44 s3_104 s3_164 s3_224 s3_284; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_46 s3_106 s3_166 s3_226 s3_286; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_13 s3_73 s3_133 s3_193 s3_253; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_22 s3_82 s3_142 s3_202 s3_262; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_20 s3_80 s3_140 s3_200 s3_260; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_57 s3_117 s3_177 s3_237 s3_297; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server20 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_40 s3_100 s3_160 s3_220 s3_280; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server21 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_29 s3_89 s3_149 s3_209 s3_269; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server22 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_37 s3_97 s3_157 s3_217 s3_277; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server23 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_14 s3_74 s3_134 s3_194 s3_254; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server24 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_59 s3_119 s3_179 s3_239 s3_299; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server25 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_50 s3_110 s3_170 s3_230 s3_290; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server26 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_55 s3_115 s3_175 s3_235 s3_295; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server27 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_15 s3_75 s3_135 s3_195 s3_255; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server28 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_61 s3_121 s3_181 s3_241; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server29 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_35 s3_95 s3_155 s3_215 s3_275; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server30 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10 s3_70 s3_130 s3_190 s3_250; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server31 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_24 s3_84 s3_144 s3_204 s3_264; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server32 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_64 s3_124 s3_184 s3_244; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server33 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_45 s3_105 s3_165 s3_225 s3_285; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server34 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_34 s3_94 s3_154 s3_214 s3_274; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server35 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_32 s3_92 s3_152 s3_212 s3_272; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server36 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_30 s3_90 s3_150 s3_210 s3_270; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server37 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_18 s3_78 s3_138 s3_198 s3_258; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server38 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_12 s3_72 s3_132 s3_192 s3_252; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server39 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_19 s3_79 s3_139 s3_199 s3_259; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server40 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_36 s3_96 s3_156 s3_216 s3_276; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server41 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8 s3_68 s3_128 s3_188 s3_248; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server42 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_49 s3_109 s3_169 s3_229 s3_289; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server43 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_48 s3_108 s3_168 s3_228 s3_288; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server44 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_17 s3_77 s3_137 s3_197 s3_257; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server45 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_16 s3_76 s3_136 s3_196 s3_256; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server46 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_26 s3_86 s3_146 s3_206 s3_266; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server47 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_53 s3_113 s3_173 s3_233 s3_293; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server48 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_39 s3_99 s3_159 s3_219 s3_279; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server49 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_38 s3_98 s3_158 s3_218 s3_278; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server50 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_21 s3_81 s3_141 s3_201 s3_261; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server51 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_9 s3_69 s3_129 s3_189 s3_249; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server52 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_63 s3_123 s3_183 s3_243; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server53 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_52 s3_112 s3_172 s3_232 s3_292; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server54 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_23 s3_83 s3_143 s3_203 s3_263; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server55 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_28 s3_88 s3_148 s3_208 s3_268; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server56 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_11 s3_71 s3_131 s3_191 s3_251; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server57 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_56 s3_116 s3_176 s3_236 s3_296; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server58 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_42 s3_102 s3_162 s3_222 s3_282; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server59 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_47 s3_107 s3_167 s3_227 s3_287; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"

    
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_58 s3_118 s3_178 s3_238 s3_298; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node root> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
