
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.201
server1=10.0.0.67
server2=10.0.0.100
server3=10.0.0.4
server4=10.0.0.106
server5=10.0.0.47
server6=10.0.0.69
server7=10.0.0.3
server8=10.0.0.102
server9=10.0.0.105
server10=10.0.0.6
server11=10.0.0.58
server12=10.0.0.49
server13=10.0.0.84
server14=10.0.0.26
server15=10.0.0.114
server16=10.0.0.86
server17=10.0.0.28
server18=10.0.0.12
server19=10.0.0.85
server20=10.0.0.29
server21=10.0.0.116
server22=10.0.0.104
server23=10.0.0.24
server24=10.0.0.23
server25=10.0.0.70
server26=10.0.0.99
server27=10.0.0.37
server28=10.0.0.55
server29=10.0.0.119
server30=10.0.0.63
server31=10.0.0.38
server32=10.0.0.45
server33=10.0.0.34
server34=10.0.0.82
server35=10.0.0.44
server36=10.0.0.16
server37=10.0.0.65
server38=10.0.0.111
server39=10.0.0.108
server40=10.0.0.79
server41=10.0.0.109
server42=10.0.0.9
server43=10.0.0.11
server44=10.0.0.71
server45=10.0.0.25
server46=10.0.0.35
server47=10.0.0.48
server48=10.0.0.74
server49=10.0.0.117
server50=10.0.0.52
server51=10.0.0.115
server52=10.0.0.18
server53=10.0.0.73
server54=10.0.0.30
server55=10.0.0.59
server56=10.0.0.96
server57=10.0.0.19
server58=10.0.0.51
server59=10.0.0.75
server60=10.0.0.76
server61=10.0.0.33
server62=10.0.0.22
server63=10.0.0.118
server64=10.0.0.57
server65=10.0.0.13
server66=10.0.0.110
server67=10.0.0.103
server68=10.0.0.27
server69=10.0.0.56
server70=10.0.0.2
server71=10.0.0.101
server72=10.0.0.10
server73=10.0.0.41
server74=10.0.0.14
server75=10.0.0.61
server76=10.0.0.80
server77=10.0.0.5
server78=10.0.0.112
server79=10.0.0.8
server80=10.0.0.68
server81=10.0.0.40
server82=10.0.0.83
server83=10.0.0.7
server84=10.0.0.98
server85=10.0.0.54
server86=10.0.0.32
server87=10.0.0.90
server88=10.0.0.43
server89=10.0.0.91
server90=10.0.0.53
server91=10.0.0.72
server92=10.0.0.113
server93=10.0.0.21
server94=10.0.0.46
server95=10.0.0.39
server96=10.0.0.92
server97=10.0.0.36
server98=10.0.0.97
server99=10.0.0.107
server100=10.0.0.81
server101=10.0.0.17
server102=10.0.0.60
server103=10.0.0.89
server104=10.0.0.88
server105=10.0.0.66
server106=10.0.0.15
server107=10.0.0.62
server108=10.0.0.95
server109=10.0.0.50
server110=10.0.0.1
server111=10.0.0.78
server112=10.0.0.94
server113=10.0.0.42
server114=10.0.0.77
server115=10.0.0.87
server116=10.0.0.93
server117=10.0.0.20
server118=10.0.0.64
server119=10.0.0.31
target=$1
no_build=$2

echo "==== STEP0: KILL OLDS ===="
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server62]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server62 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server63]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server63 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server66]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server66 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server68]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server68 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server69]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server69 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server70]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server70 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server72]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server72 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server73]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server73 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server74]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server74 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server75]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server75 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server80]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server80 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server82]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server82 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server83]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server83 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server84]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server84 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server87]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server87 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server88]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server88 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server91]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server91 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server93]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server93 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server95]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server95 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server96]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server96 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server98]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server98 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server99]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server99 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server101]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server101 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server104]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server104 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server112]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server112 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server114]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server114 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server116]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server116 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server117]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server117 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<"EOF"
ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /mnt && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/mnt' r1 s3_118 s3_238
echo "==== 同步中继服务器 ====" 

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/mnt' s3_65 s3_185;

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/mnt' s3_98 s3_218;

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/mnt' s3_2 s3_122 s3_242;

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/mnt' s3_104 s3_224;

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/mnt' s3_45 s3_165 s3_285;

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/mnt' s3_67 s3_187;

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/mnt' s3_1 s3_121 s3_241;

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/mnt' s3_100 s3_220;

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server9} 'Xf4aGbTaf!' '/mnt' s3_103 s3_223;

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server10} 'Xf4aGbTaf!' '/mnt' s3_4 s3_124 s3_244;

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server0} ${server11} 'Xf4aGbTaf!' '/mnt' s3_56 s3_176 s3_296;

EOF
) &

wait
echo "==== 同步其他服务器 ====" 

(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server12} 'Xf4aGbTaf!' '/mnt' s3_47 s3_167 s3_287;

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server13} 'Xf4aGbTaf!' '/mnt' s3_82 s3_202;

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server14} 'Xf4aGbTaf!' '/mnt' s3_24 s3_144 s3_264;

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server15} 'Xf4aGbTaf!' '/mnt' s3_112 s3_232;

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server16} 'Xf4aGbTaf!' '/mnt' s3_84 s3_204;

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server17} 'Xf4aGbTaf!' '/mnt' s3_26 s3_146 s3_266;

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server18} 'Xf4aGbTaf!' '/mnt' s3_10 s3_130 s3_250;

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server19} 'Xf4aGbTaf!' '/mnt' s3_83 s3_203;

EOF
) &


(
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server20} 'Xf4aGbTaf!' '/mnt' s3_27 s3_147 s3_267;

EOF
) &


(
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server21} 'Xf4aGbTaf!' '/mnt' s3_114 s3_234;

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server22} 'Xf4aGbTaf!' '/mnt' s3_102 s3_222;

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server23} 'Xf4aGbTaf!' '/mnt' s3_22 s3_142 s3_262;

EOF
) &


(
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server24} 'Xf4aGbTaf!' '/mnt' s3_21 s3_141 s3_261;

EOF
) &


(
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server25} 'Xf4aGbTaf!' '/mnt' s3_68 s3_188;

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server26} 'Xf4aGbTaf!' '/mnt' s3_97 s3_217;

EOF
) &


(
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server27} 'Xf4aGbTaf!' '/mnt' s3_35 s3_155 s3_275;

EOF
) &


(
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server28} 'Xf4aGbTaf!' '/mnt' s3_53 s3_173 s3_293;

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server29} 'Xf4aGbTaf!' '/mnt' s3_117 s3_237;

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server30} 'Xf4aGbTaf!' '/mnt' s3_61 s3_181;

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server31} 'Xf4aGbTaf!' '/mnt' s3_36 s3_156 s3_276;

EOF
) &


(
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server32} 'Xf4aGbTaf!' '/mnt' s3_43 s3_163 s3_283;

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server33} 'Xf4aGbTaf!' '/mnt' s3_32 s3_152 s3_272;

EOF
) &


(
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server34} 'Xf4aGbTaf!' '/mnt' s3_80 s3_200;

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server35} 'Xf4aGbTaf!' '/mnt' s3_42 s3_162 s3_282;

EOF
) &


(
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server36} 'Xf4aGbTaf!' '/mnt' s3_14 s3_134 s3_254;

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server37} 'Xf4aGbTaf!' '/mnt' s3_63 s3_183;

EOF
) &


(
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server38} 'Xf4aGbTaf!' '/mnt' s3_109 s3_229;

EOF
) &


(
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server39} 'Xf4aGbTaf!' '/mnt' s3_106 s3_226;

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server40} 'Xf4aGbTaf!' '/mnt' s3_77 s3_197;

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server41} 'Xf4aGbTaf!' '/mnt' s3_107 s3_227;

EOF
) &


(
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server42} 'Xf4aGbTaf!' '/mnt' s3_7 s3_127 s3_247;

EOF
) &


(
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server43} 'Xf4aGbTaf!' '/mnt' s3_9 s3_129 s3_249;

EOF
) &


(
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server44} 'Xf4aGbTaf!' '/mnt' s3_69 s3_189;

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server45} 'Xf4aGbTaf!' '/mnt' s3_23 s3_143 s3_263;

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server46} 'Xf4aGbTaf!' '/mnt' s3_33 s3_153 s3_273;

EOF
) &


(
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server47} 'Xf4aGbTaf!' '/mnt' s3_46 s3_166 s3_286;

EOF
) &


(
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server48} 'Xf4aGbTaf!' '/mnt' s3_72 s3_192;

EOF
) &


(
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server49} 'Xf4aGbTaf!' '/mnt' s3_115 s3_235;

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server50} 'Xf4aGbTaf!' '/mnt' s3_50 s3_170 s3_290;

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server51} 'Xf4aGbTaf!' '/mnt' s3_113 s3_233;

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server52} 'Xf4aGbTaf!' '/mnt' s3_16 s3_136 s3_256;

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server53} 'Xf4aGbTaf!' '/mnt' s3_71 s3_191;

EOF
) &


(
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server54} 'Xf4aGbTaf!' '/mnt' s3_28 s3_148 s3_268;

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server55} 'Xf4aGbTaf!' '/mnt' s3_57 s3_177 s3_297;

EOF
) &


(
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server56} 'Xf4aGbTaf!' '/mnt' s3_94 s3_214;

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server57} 'Xf4aGbTaf!' '/mnt' s3_17 s3_137 s3_257;

EOF
) &


(
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server58} 'Xf4aGbTaf!' '/mnt' s3_49 s3_169 s3_289;

EOF
) &


(
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server59} 'Xf4aGbTaf!' '/mnt' s3_73 s3_193;

EOF
) &


(
echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server60} 'Xf4aGbTaf!' '/mnt' s3_74 s3_194;

EOF
) &


(
echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server61} 'Xf4aGbTaf!' '/mnt' s3_31 s3_151 s3_271;

EOF
) &


(
echo "[$server62]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server62 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server62} 'Xf4aGbTaf!' '/mnt' s3_20 s3_140 s3_260;

EOF
) &


(
echo "[$server63]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server63 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server63} 'Xf4aGbTaf!' '/mnt' s3_116 s3_236;

EOF
) &


(
echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server64} 'Xf4aGbTaf!' '/mnt' s3_55 s3_175 s3_295;

EOF
) &


(
echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server65} 'Xf4aGbTaf!' '/mnt' s3_11 s3_131 s3_251;

EOF
) &


(
echo "[$server66]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server66 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server66} 'Xf4aGbTaf!' '/mnt' s3_108 s3_228;

EOF
) &


(
echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server67} 'Xf4aGbTaf!' '/mnt' s3_101 s3_221;

EOF
) &


(
echo "[$server68]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server68 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server68} 'Xf4aGbTaf!' '/mnt' s3_25 s3_145 s3_265;

EOF
) &


(
echo "[$server69]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server69 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server69} 'Xf4aGbTaf!' '/mnt' s3_54 s3_174 s3_294;

EOF
) &


(
echo "[$server70]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server70 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server70} 'Xf4aGbTaf!' '/mnt' r3 s3_120 s3_240;

EOF
) &


(
echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server71} 'Xf4aGbTaf!' '/mnt' s3_99 s3_219;

EOF
) &


(
echo "[$server72]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server72 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server72} 'Xf4aGbTaf!' '/mnt' s3_8 s3_128 s3_248;

EOF
) &


(
echo "[$server73]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server73 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server73} 'Xf4aGbTaf!' '/mnt' s3_39 s3_159 s3_279;

EOF
) &


(
echo "[$server74]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server74 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server74} 'Xf4aGbTaf!' '/mnt' s3_12 s3_132 s3_252;

EOF
) &


(
echo "[$server75]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server75 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server75} 'Xf4aGbTaf!' '/mnt' s3_59 s3_179 s3_299;

EOF
) &


(
echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server76} 'Xf4aGbTaf!' '/mnt' s3_78 s3_198;

EOF
) &


(
echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server77} 'Xf4aGbTaf!' '/mnt' s3_3 s3_123 s3_243;

EOF
) &


(
echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server78} 'Xf4aGbTaf!' '/mnt' s3_110 s3_230;

EOF
) &


(
echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server79} 'Xf4aGbTaf!' '/mnt' s3_6 s3_126 s3_246;

EOF
) &


(
echo "[$server80]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server80 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server80} 'Xf4aGbTaf!' '/mnt' s3_66 s3_186;

EOF
) &


(
echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server81} 'Xf4aGbTaf!' '/mnt' s3_38 s3_158 s3_278;

EOF
) &


(
echo "[$server82]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server82 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server82} 'Xf4aGbTaf!' '/mnt' s3_81 s3_201;

EOF
) &


(
echo "[$server83]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server83 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server83} 'Xf4aGbTaf!' '/mnt' s3_5 s3_125 s3_245;

EOF
) &


(
echo "[$server84]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server84 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server84} 'Xf4aGbTaf!' '/mnt' s3_96 s3_216;

EOF
) &


(
echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server85} 'Xf4aGbTaf!' '/mnt' s3_52 s3_172 s3_292;

EOF
) &


(
echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server86} 'Xf4aGbTaf!' '/mnt' s3_30 s3_150 s3_270;

EOF
) &


(
echo "[$server87]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server87 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server87} 'Xf4aGbTaf!' '/mnt' s3_88 s3_208;

EOF
) &


(
echo "[$server88]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server88 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server88} 'Xf4aGbTaf!' '/mnt' s3_41 s3_161 s3_281;

EOF
) &


(
echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server89} 'Xf4aGbTaf!' '/mnt' s3_89 s3_209;

EOF
) &


(
echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server90} 'Xf4aGbTaf!' '/mnt' s3_51 s3_171 s3_291;

EOF
) &


(
echo "[$server91]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server91 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server91} 'Xf4aGbTaf!' '/mnt' s3_70 s3_190;

EOF
) &


(
echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server92} 'Xf4aGbTaf!' '/mnt' s3_111 s3_231;

EOF
) &


(
echo "[$server93]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server93 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server93} 'Xf4aGbTaf!' '/mnt' s3_19 s3_139 s3_259;

EOF
) &


(
echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server94} 'Xf4aGbTaf!' '/mnt' s3_44 s3_164 s3_284;

EOF
) &


(
echo "[$server95]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server95 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server95} 'Xf4aGbTaf!' '/mnt' s3_37 s3_157 s3_277;

EOF
) &


(
echo "[$server96]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server96 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server96} 'Xf4aGbTaf!' '/mnt' s3_90 s3_210;

EOF
) &


(
echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server97} 'Xf4aGbTaf!' '/mnt' s3_34 s3_154 s3_274;

EOF
) &


(
echo "[$server98]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server98 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server98} 'Xf4aGbTaf!' '/mnt' s3_95 s3_215;

EOF
) &


(
echo "[$server99]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server99 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server99} 'Xf4aGbTaf!' '/mnt' s3_105 s3_225;

EOF
) &


(
echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server100} 'Xf4aGbTaf!' '/mnt' s3_79 s3_199;

EOF
) &


(
echo "[$server101]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server101 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server101} 'Xf4aGbTaf!' '/mnt' s3_15 s3_135 s3_255;

EOF
) &


(
echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server102} 'Xf4aGbTaf!' '/mnt' s3_58 s3_178 s3_298;

EOF
) &


(
echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server103} 'Xf4aGbTaf!' '/mnt' s3_87 s3_207;

EOF
) &


(
echo "[$server104]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server104 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server104} 'Xf4aGbTaf!' '/mnt' s3_86 s3_206;

EOF
) &


(
echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server105} 'Xf4aGbTaf!' '/mnt' s3_64 s3_184;

EOF
) &


(
echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server106} 'Xf4aGbTaf!' '/mnt' s3_13 s3_133 s3_253;

EOF
) &


(
echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server107} 'Xf4aGbTaf!' '/mnt' s3_60 s3_180 s3_300;

EOF
) &


(
echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server108} 'Xf4aGbTaf!' '/mnt' s3_93 s3_213;

EOF
) &


(
echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server11} ${server109} 'Xf4aGbTaf!' '/mnt' s3_48 s3_168 s3_288;

EOF
) &


(
echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server1} ${server110} 'Xf4aGbTaf!' '/mnt' r2 s3_119 s3_239;

EOF
) &


(
echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server2} ${server111} 'Xf4aGbTaf!' '/mnt' s3_76 s3_196;

EOF
) &


(
echo "[$server112]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server112 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server3} ${server112} 'Xf4aGbTaf!' '/mnt' s3_92 s3_212;

EOF
) &


(
echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server4} ${server113} 'Xf4aGbTaf!' '/mnt' s3_40 s3_160 s3_280;

EOF
) &


(
echo "[$server114]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server114 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server5} ${server114} 'Xf4aGbTaf!' '/mnt' s3_75 s3_195;

EOF
) &


(
echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server6} ${server115} 'Xf4aGbTaf!' '/mnt' s3_85 s3_205;

EOF
) &


(
echo "[$server116]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server116 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server7} ${server116} 'Xf4aGbTaf!' '/mnt' s3_91 s3_211;

EOF
) &


(
echo "[$server117]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server117 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server8} ${server117} 'Xf4aGbTaf!' '/mnt' s3_18 s3_138 s3_258;

EOF
) &


(
echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server9} ${server118} 'Xf4aGbTaf!' '/mnt' s3_62 s3_182;

EOF
) &


(
echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<EOF
mkdir -p /mnt;
rm -rf /mnt/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/mnt/fetch.sh /mnt/
cd /mnt && sh -x fetch.sh ${server10} ${server119} 'Xf4aGbTaf!' '/mnt' s3_29 s3_149 s3_269;

EOF
) &

wait

(
echo "[$server0]"
for n in r1 s3_118 s3_238; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/${n}
done
) &


(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
for n in s3_65 s3_185; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_65 s3_185; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
for n in s3_98 s3_218; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_98 s3_218; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
for n in s3_2 s3_122 s3_242; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_2 s3_122 s3_242; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
for n in s3_104 s3_224; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_104 s3_224; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
for n in s3_45 s3_165 s3_285; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_45 s3_165 s3_285; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
for n in s3_67 s3_187; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_67 s3_187; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
for n in s3_1 s3_121 s3_241; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_1 s3_121 s3_241; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
for n in s3_100 s3_220; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_100 s3_220; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
for n in s3_103 s3_223; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_103 s3_223; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
for n in s3_4 s3_124 s3_244; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_4 s3_124 s3_244; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
for n in s3_56 s3_176 s3_296; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_56 s3_176 s3_296; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
for n in s3_47 s3_167 s3_287; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_47 s3_167 s3_287; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
for n in s3_82 s3_202; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_82 s3_202; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
for n in s3_24 s3_144 s3_264; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_24 s3_144 s3_264; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
for n in s3_112 s3_232; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_112 s3_232; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
for n in s3_84 s3_204; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_84 s3_204; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
for n in s3_26 s3_146 s3_266; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_26 s3_146 s3_266; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
for n in s3_10 s3_130 s3_250; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_10 s3_130 s3_250; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
for n in s3_83 s3_203; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_83 s3_203; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
for n in s3_27 s3_147 s3_267; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_27 s3_147 s3_267; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
for n in s3_114 s3_234; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_114 s3_234; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
for n in s3_102 s3_222; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_102 s3_222; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
for n in s3_22 s3_142 s3_262; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_22 s3_142 s3_262; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
for n in s3_21 s3_141 s3_261; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_21 s3_141 s3_261; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
for n in s3_68 s3_188; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_68 s3_188; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
for n in s3_97 s3_217; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_97 s3_217; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
for n in s3_35 s3_155 s3_275; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_35 s3_155 s3_275; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
for n in s3_53 s3_173 s3_293; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_53 s3_173 s3_293; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
for n in s3_117 s3_237; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_117 s3_237; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
for n in s3_61 s3_181; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_61 s3_181; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
for n in s3_36 s3_156 s3_276; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_36 s3_156 s3_276; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
for n in s3_43 s3_163 s3_283; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_43 s3_163 s3_283; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
for n in s3_32 s3_152 s3_272; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_32 s3_152 s3_272; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
for n in s3_80 s3_200; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_80 s3_200; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
for n in s3_42 s3_162 s3_282; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_42 s3_162 s3_282; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
for n in s3_14 s3_134 s3_254; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_14 s3_134 s3_254; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
for n in s3_63 s3_183; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_63 s3_183; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
for n in s3_109 s3_229; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_109 s3_229; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
for n in s3_106 s3_226; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_106 s3_226; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
for n in s3_77 s3_197; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_77 s3_197; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
for n in s3_107 s3_227; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_107 s3_227; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
for n in s3_7 s3_127 s3_247; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_7 s3_127 s3_247; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
for n in s3_9 s3_129 s3_249; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_9 s3_129 s3_249; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
for n in s3_69 s3_189; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_69 s3_189; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
for n in s3_23 s3_143 s3_263; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_23 s3_143 s3_263; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
for n in s3_33 s3_153 s3_273; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_33 s3_153 s3_273; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
for n in s3_46 s3_166 s3_286; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_46 s3_166 s3_286; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
for n in s3_72 s3_192; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_72 s3_192; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
for n in s3_115 s3_235; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_115 s3_235; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
for n in s3_50 s3_170 s3_290; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_50 s3_170 s3_290; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
for n in s3_113 s3_233; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_113 s3_233; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
for n in s3_16 s3_136 s3_256; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_16 s3_136 s3_256; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
for n in s3_71 s3_191; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_71 s3_191; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
for n in s3_28 s3_148 s3_268; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_28 s3_148 s3_268; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
for n in s3_57 s3_177 s3_297; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_57 s3_177 s3_297; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
for n in s3_94 s3_214; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_94 s3_214; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
for n in s3_17 s3_137 s3_257; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_17 s3_137 s3_257; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
for n in s3_49 s3_169 s3_289; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_49 s3_169 s3_289; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
for n in s3_73 s3_193; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_73 s3_193; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<EOF
for n in s3_74 s3_194; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_74 s3_194; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<EOF
for n in s3_31 s3_151 s3_271; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_31 s3_151 s3_271; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server62]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server62 <<EOF
for n in s3_20 s3_140 s3_260; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_20 s3_140 s3_260; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server63]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server63 <<EOF
for n in s3_116 s3_236; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_116 s3_236; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<EOF
for n in s3_55 s3_175 s3_295; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_55 s3_175 s3_295; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<EOF
for n in s3_11 s3_131 s3_251; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_11 s3_131 s3_251; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server66]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server66 <<EOF
for n in s3_108 s3_228; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_108 s3_228; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<EOF
for n in s3_101 s3_221; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_101 s3_221; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server68]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server68 <<EOF
for n in s3_25 s3_145 s3_265; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_25 s3_145 s3_265; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server69]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server69 <<EOF
for n in s3_54 s3_174 s3_294; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_54 s3_174 s3_294; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server70]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server70 <<EOF
for n in r3 s3_120 s3_240; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in r3; do
    cp -rf /mnt/zjnodes/zjchain/root_db /mnt/zjnodes/\${n}/db
done

for n in s3_120 s3_240; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<EOF
for n in s3_99 s3_219; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_99 s3_219; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server72]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server72 <<EOF
for n in s3_8 s3_128 s3_248; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_8 s3_128 s3_248; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server73]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server73 <<EOF
for n in s3_39 s3_159 s3_279; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_39 s3_159 s3_279; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server74]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server74 <<EOF
for n in s3_12 s3_132 s3_252; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_12 s3_132 s3_252; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server75]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server75 <<EOF
for n in s3_59 s3_179 s3_299; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_59 s3_179 s3_299; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<EOF
for n in s3_78 s3_198; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_78 s3_198; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<EOF
for n in s3_3 s3_123 s3_243; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_3 s3_123 s3_243; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<EOF
for n in s3_110 s3_230; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_110 s3_230; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<EOF
for n in s3_6 s3_126 s3_246; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_6 s3_126 s3_246; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server80]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server80 <<EOF
for n in s3_66 s3_186; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_66 s3_186; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<EOF
for n in s3_38 s3_158 s3_278; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_38 s3_158 s3_278; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server82]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server82 <<EOF
for n in s3_81 s3_201; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_81 s3_201; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server83]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server83 <<EOF
for n in s3_5 s3_125 s3_245; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_5 s3_125 s3_245; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server84]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server84 <<EOF
for n in s3_96 s3_216; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_96 s3_216; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<EOF
for n in s3_52 s3_172 s3_292; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_52 s3_172 s3_292; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<EOF
for n in s3_30 s3_150 s3_270; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_30 s3_150 s3_270; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server87]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server87 <<EOF
for n in s3_88 s3_208; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_88 s3_208; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server88]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server88 <<EOF
for n in s3_41 s3_161 s3_281; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_41 s3_161 s3_281; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<EOF
for n in s3_89 s3_209; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_89 s3_209; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<EOF
for n in s3_51 s3_171 s3_291; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_51 s3_171 s3_291; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server91]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server91 <<EOF
for n in s3_70 s3_190; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_70 s3_190; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<EOF
for n in s3_111 s3_231; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_111 s3_231; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server93]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server93 <<EOF
for n in s3_19 s3_139 s3_259; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_19 s3_139 s3_259; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<EOF
for n in s3_44 s3_164 s3_284; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_44 s3_164 s3_284; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server95]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server95 <<EOF
for n in s3_37 s3_157 s3_277; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_37 s3_157 s3_277; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server96]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server96 <<EOF
for n in s3_90 s3_210; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_90 s3_210; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<EOF
for n in s3_34 s3_154 s3_274; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_34 s3_154 s3_274; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server98]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server98 <<EOF
for n in s3_95 s3_215; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_95 s3_215; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server99]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server99 <<EOF
for n in s3_105 s3_225; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_105 s3_225; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<EOF
for n in s3_79 s3_199; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_79 s3_199; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server101]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server101 <<EOF
for n in s3_15 s3_135 s3_255; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_15 s3_135 s3_255; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<EOF
for n in s3_58 s3_178 s3_298; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_58 s3_178 s3_298; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<EOF
for n in s3_87 s3_207; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_87 s3_207; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server104]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server104 <<EOF
for n in s3_86 s3_206; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_86 s3_206; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<EOF
for n in s3_64 s3_184; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_64 s3_184; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<EOF
for n in s3_13 s3_133 s3_253; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_13 s3_133 s3_253; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<EOF
for n in s3_60 s3_180 s3_300; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_60 s3_180 s3_300; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<EOF
for n in s3_93 s3_213; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_93 s3_213; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<EOF
for n in s3_48 s3_168 s3_288; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_48 s3_168 s3_288; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<EOF
for n in r2 s3_119 s3_239; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in r2; do
    cp -rf /mnt/zjnodes/zjchain/root_db /mnt/zjnodes/\${n}/db
done

for n in s3_119 s3_239; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<EOF
for n in s3_76 s3_196; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_76 s3_196; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server112]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server112 <<EOF
for n in s3_92 s3_212; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_92 s3_212; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<EOF
for n in s3_40 s3_160 s3_280; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_40 s3_160 s3_280; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server114]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server114 <<EOF
for n in s3_75 s3_195; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_75 s3_195; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<EOF
for n in s3_85 s3_205; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_85 s3_205; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server116]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server116 <<EOF
for n in s3_91 s3_211; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_91 s3_211; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server117]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server117 <<EOF
for n in s3_18 s3_138 s3_258; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_18 s3_138 s3_258; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<EOF
for n in s3_62 s3_182; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_62 s3_182; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &


(
echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<EOF
for n in s3_29 s3_149 s3_269; do
    ln -s /mnt/zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/\${n}/conf
    ln -s /mnt/zjnodes/zjchain/zjchain /mnt/zjnodes/\${n}
done

for n in s3_29 s3_149 s3_269; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/\${n}/db
done

EOF
) &

(

for n in r1; do
    cp -rf /mnt/zjnodes/zjchain/root_db /mnt/zjnodes/${n}/db
done

for n in s3_118 s3_238; do
    cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/${n}/db
done
) &
wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server2]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server3]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server4]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server5]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server6]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server7]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server8]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server9]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server10]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server11]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server12]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server13]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server14]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server15]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server16]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server17]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server18]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server19]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server20]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server20 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server21]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server21 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server22]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server23]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server24]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server24 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server25]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server25 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server26]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server27]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server27 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server28]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server28 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server29]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server30]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server31]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server32]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server32 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server33]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server34]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server34 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server35]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server36]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server36 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server37]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server38]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server38 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server39]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server39 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server40]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server41]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server42]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server42 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server43]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server43 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server44]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server44 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server45]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server46]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server47]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server47 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server48]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server48 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server49]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server49 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server50]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server51]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server52]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server53]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server54]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server54 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server55]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server56]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server56 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server57]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server58]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server58 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server59]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server59 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server60]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server61]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server62]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server62 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server63]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server63 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server64]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server65]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server66]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server66 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server67]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server68]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server68 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server69]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server69 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server70]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server70 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server71]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server72]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server72 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server73]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server73 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server74]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server74 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server75]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server75 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server76]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server77]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server78]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server79]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server80]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server80 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server81]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server82]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server82 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server83]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server83 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server84]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server84 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server85]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server86]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server87]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server87 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server88]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server88 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server89]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server90]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server91]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server91 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server92]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server93]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server93 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server94]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server95]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server95 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server96]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server96 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server97]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server98]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server98 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server99]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server99 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server100]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server101]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server101 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server102]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server103]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server104]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server104 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server105]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server106]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server107]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server108]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server109]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server110]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server111]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server112]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server112 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server113]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server114]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server114 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server115]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server116]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server116 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server117]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server117 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server118]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server119]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<"EOF"
# ps -ef | grep zjchain | grep mnt | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /mnt/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 mnt> /dev/null 2>&1 &

sleep 3

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_65 s3_185; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server2]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_98 s3_218; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server3]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_122 s3_242; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_104 s3_224; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_45 s3_165 s3_285; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server6]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_67 s3_187; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server7]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_121 s3_241; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server8]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_100 s3_220; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_103 s3_223; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server10]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_124 s3_244; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_56 s3_176 s3_296; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_47 s3_167 s3_287; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server13]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_82 s3_202; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server14]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_24 s3_144 s3_264; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server15]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_112 s3_232; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_84 s3_204; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_26 s3_146 s3_266; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server18]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10 s3_130 s3_250; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server19]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_83 s3_203; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server20]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server20 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_27 s3_147 s3_267; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server21]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server21 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_114 s3_234; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server22 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_102 s3_222; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server23 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_22 s3_142 s3_262; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server24]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server24 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_21 s3_141 s3_261; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server25]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server25 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_68 s3_188; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server26 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_97 s3_217; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server27]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server27 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_35 s3_155 s3_275; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server28]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server28 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_53 s3_173 s3_293; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server29 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_117 s3_237; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server30 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_61 s3_181; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server31 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_36 s3_156 s3_276; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server32]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server32 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_43 s3_163 s3_283; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server33 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_32 s3_152 s3_272; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server34]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server34 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_80 s3_200; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server35 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_42 s3_162 s3_282; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server36]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server36 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_14 s3_134 s3_254; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server37 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_63 s3_183; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server38]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server38 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_109 s3_229; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server39]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server39 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_106 s3_226; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server40 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_77 s3_197; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server41 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_107 s3_227; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server42]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server42 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_127 s3_247; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server43]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server43 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_9 s3_129 s3_249; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server44]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server44 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_69 s3_189; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server45 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_23 s3_143 s3_263; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server46 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_33 s3_153 s3_273; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server47]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server47 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_46 s3_166 s3_286; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server48]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server48 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_72 s3_192; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server49]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server49 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_115 s3_235; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server50 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_50 s3_170 s3_290; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server51 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_113 s3_233; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server52 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_16 s3_136 s3_256; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server53 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_71 s3_191; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server54]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server54 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_28 s3_148 s3_268; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server55 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_57 s3_177 s3_297; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server56]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server56 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_94 s3_214; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server57 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_17 s3_137 s3_257; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server58]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server58 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_49 s3_169 s3_289; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server59]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server59 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_73 s3_193; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server60 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_74 s3_194; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server61 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_31 s3_151 s3_271; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server62]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server62 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_20 s3_140 s3_260; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server63]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server63 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_116 s3_236; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server64 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_55 s3_175 s3_295; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server65 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_11 s3_131 s3_251; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server66]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server66 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_108 s3_228; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server67 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_101 s3_221; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server68]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server68 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_25 s3_145 s3_265; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server69]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server69 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_54 s3_174 s3_294; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server70]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server70 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_120 s3_240; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server71 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_99 s3_219; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server72]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server72 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8 s3_128 s3_248; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server73]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server73 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_39 s3_159 s3_279; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server74]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server74 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_12 s3_132 s3_252; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server75]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server75 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_59 s3_179 s3_299; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server76 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_78 s3_198; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server77 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_123 s3_243; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server78 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_110 s3_230; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server79 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_126 s3_246; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server80]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server80 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_66 s3_186; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server81 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_38 s3_158 s3_278; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server82]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server82 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_81 s3_201; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server83]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server83 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5 s3_125 s3_245; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server84]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server84 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_96 s3_216; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server85 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_52 s3_172 s3_292; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server86 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_30 s3_150 s3_270; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server87]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server87 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_88 s3_208; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server88]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server88 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_41 s3_161 s3_281; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server89 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_89 s3_209; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server90 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_51 s3_171 s3_291; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server91]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server91 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_70 s3_190; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server92 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_111 s3_231; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server93]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server93 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_19 s3_139 s3_259; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server94 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_44 s3_164 s3_284; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server95]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server95 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_37 s3_157 s3_277; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server96]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server96 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_90 s3_210; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server97 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_34 s3_154 s3_274; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server98]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server98 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_95 s3_215; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server99]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server99 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_105 s3_225; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server100 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_79 s3_199; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server101]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server101 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_15 s3_135 s3_255; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server102 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_58 s3_178 s3_298; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server103 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_87 s3_207; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server104]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server104 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_86 s3_206; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server105 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_64 s3_184; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server106 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_13 s3_133 s3_253; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server107 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_60 s3_180 s3_300; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server108 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_93 s3_213; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server109 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_48 s3_168 s3_288; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server110 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_119 s3_239; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server111 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_76 s3_196; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server112]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server112 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_92 s3_212; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server113 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_40 s3_160 s3_280; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server114]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server114 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_75 s3_195; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server115 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_85 s3_205; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server116]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server116 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_91 s3_211; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server117]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server117 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_18 s3_138 s3_258; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server118 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_62 s3_182; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server119 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_29 s3_149 s3_269; do \
    cd /mnt/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node mnt> /dev/null 2>&1 &\
done \
'"

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_118 s3_238; do
cd /mnt/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node mnt> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
