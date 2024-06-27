
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.0.0.201
server1=10.0.0.18
server2=10.0.0.91
server3=10.0.0.99
server4=10.0.0.6
server5=10.0.0.20
server6=10.0.0.80
server7=10.0.0.112
server8=10.0.0.89
server9=10.0.0.56
server10=10.0.0.100
server11=10.0.0.1
server12=10.0.0.37
server13=10.0.0.67
server14=10.0.0.95
server15=10.0.0.60
server16=10.0.0.4
server17=10.0.0.15
server18=10.0.0.98
server19=10.0.0.105
server20=10.0.0.85
server21=10.0.0.113
server22=10.0.0.22
server23=10.0.0.36
server24=10.0.0.108
server25=10.0.0.63
server26=10.0.0.5
server27=10.0.0.81
server28=10.0.0.110
server29=10.0.0.17
server30=10.0.0.46
server31=10.0.0.24
server32=10.0.0.77
server33=10.0.0.55
server34=10.0.0.76
server35=10.0.0.57
server36=10.0.0.117
server37=10.0.0.54
server38=10.0.0.64
server39=10.0.0.104
server40=10.0.0.7
server41=10.0.0.14
server42=10.0.0.96
server43=10.0.0.92
server44=10.0.0.72
server45=10.0.0.42
server46=10.0.0.43
server47=10.0.0.94
server48=10.0.0.88
server49=10.0.0.111
server50=10.0.0.51
server51=10.0.0.41
server52=10.0.0.2
server53=10.0.0.50
server54=10.0.0.90
server55=10.0.0.31
server56=10.0.0.66
server57=10.0.0.33
server58=10.0.0.97
server59=10.0.0.70
server60=10.0.0.52
server61=10.0.0.45
server62=10.0.0.86
server63=10.0.0.69
server64=10.0.0.39
server65=10.0.0.26
server66=10.0.0.103
server67=10.0.0.13
server68=10.0.0.119
server69=10.0.0.106
server70=10.0.0.109
server71=10.0.0.34
server72=10.0.0.65
server73=10.0.0.114
server74=10.0.0.115
server75=10.0.0.79
server76=10.0.0.16
server77=10.0.0.27
server78=10.0.0.40
server79=10.0.0.53
server80=10.0.0.93
server81=10.0.0.48
server82=10.0.0.62
server83=10.0.0.83
server84=10.0.0.82
server85=10.0.0.47
server86=10.0.0.35
server87=10.0.0.107
server88=10.0.0.73
server89=10.0.0.58
server90=10.0.0.25
server91=10.0.0.71
server92=10.0.0.28
server93=10.0.0.116
server94=10.0.0.21
server95=10.0.0.68
server96=10.0.0.74
server97=10.0.0.59
server98=10.0.0.102
server99=10.0.0.101
server100=10.0.0.49
server101=10.0.0.78
server102=10.0.0.10
server103=10.0.0.19
server104=10.0.0.61
server105=10.0.0.32
server106=10.0.0.30
server107=10.0.0.3
server108=10.0.0.29
server109=10.0.0.23
server110=10.0.0.8
server111=10.0.0.9
server112=10.0.0.118
server113=10.0.0.38
server114=10.0.0.84
server115=10.0.0.12
server116=10.0.0.75
server117=10.0.0.87
server118=10.0.0.11
server119=10.0.0.44
target=$1
no_build=$2

echo "==== STEP0: KILL OLDS ===="
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9

echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server2]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server3]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server7]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server8]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server10]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server14]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server15]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server19]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server20]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server20 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server21]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server21 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server24 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server25]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server25 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server27]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server27 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server28]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server28 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server32 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server34]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server34 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server36]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server36 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server38]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server38 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server39]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server39 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server42 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server43]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server43 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server44]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server44 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server47 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server48]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server48 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server49]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server49 <<"EOF"
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
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server54 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server56]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server56 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server58]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server58 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server59]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server59 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server62]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server62 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server63]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server63 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server66]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server66 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server68]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server68 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server69]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server69 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server70]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server70 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server72]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server72 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server73]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server73 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server74]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server74 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server75]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server75 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server80]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server80 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server82]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server82 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server83]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server83 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server84]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server84 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server87]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server87 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server88]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server88 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server91]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server91 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server93]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server93 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server95]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server95 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server96]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server96 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server98]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server98 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server99]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server99 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server101]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server101 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server104]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server104 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server112]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server112 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server114]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server114 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server116]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server116 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server117]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server117 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<"EOF"
ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server0]"
sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} 'Xf4aGbTaf!' '/root' r1 s3_118 s3_238 s3_358 s3_478 s3_598
echo "==== 同步中继服务器 ====" 

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} 'Xf4aGbTaf!' '/root' s3_16 s3_136 s3_256 s3_376 s3_496;

EOF
) &


(
echo "[$server2]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server2} 'Xf4aGbTaf!' '/root' s3_89 s3_209 s3_329 s3_449 s3_569;

EOF
) &


(
echo "[$server3]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server3} 'Xf4aGbTaf!' '/root' s3_97 s3_217 s3_337 s3_457 s3_577;

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server4} 'Xf4aGbTaf!' '/root' s3_4 s3_124 s3_244 s3_364 s3_484;

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server5} 'Xf4aGbTaf!' '/root' s3_18 s3_138 s3_258 s3_378 s3_498;

EOF
) &


(
echo "[$server6]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server6} 'Xf4aGbTaf!' '/root' s3_78 s3_198 s3_318 s3_438 s3_558;

EOF
) &


(
echo "[$server7]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server7} 'Xf4aGbTaf!' '/root' s3_110 s3_230 s3_350 s3_470 s3_590;

EOF
) &


(
echo "[$server8]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server8} 'Xf4aGbTaf!' '/root' s3_87 s3_207 s3_327 s3_447 s3_567;

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server9} 'Xf4aGbTaf!' '/root' s3_54 s3_174 s3_294 s3_414 s3_534;

EOF
) &


(
echo "[$server10]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server10} 'Xf4aGbTaf!' '/root' s3_98 s3_218 s3_338 s3_458 s3_578;

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server11} 'Xf4aGbTaf!' '/root' r2 s3_119 s3_239 s3_359 s3_479 s3_599;

EOF
) &

wait
echo "==== 同步其他服务器 ====" 

(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server12} '' '/root' s3_35 s3_155 s3_275 s3_395 s3_515;

EOF
) &


(
echo "[$server13]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server13} '' '/root' s3_65 s3_185 s3_305 s3_425 s3_545;

EOF
) &


(
echo "[$server14]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server14} 'Xf4aGbTaf!' '/root' s3_93 s3_213 s3_333 s3_453 s3_573;

EOF
) &


(
echo "[$server15]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server15} 'Xf4aGbTaf!' '/root' s3_58 s3_178 s3_298 s3_418 s3_538;

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server16} '' '/root' s3_2 s3_122 s3_242 s3_362 s3_482;

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server17} '' '/root' s3_13 s3_133 s3_253 s3_373 s3_493;

EOF
) &


(
echo "[$server18]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server18} '' '/root' s3_96 s3_216 s3_336 s3_456 s3_576;

EOF
) &


(
echo "[$server19]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server19} 'Xf4aGbTaf!' '/root' s3_103 s3_223 s3_343 s3_463 s3_583;

EOF
) &


(
echo "[$server20]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server20} '' '/root' s3_83 s3_203 s3_323 s3_443 s3_563;

EOF
) &


(
echo "[$server21]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server21} 'Xf4aGbTaf!' '/root' s3_111 s3_231 s3_351 s3_471 s3_591;

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server22} 'Xf4aGbTaf!' '/root' s3_20 s3_140 s3_260 s3_380 s3_500;

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server23} '' '/root' s3_34 s3_154 s3_274 s3_394 s3_514;

EOF
) &


(
echo "[$server24]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server24} '' '/root' s3_106 s3_226 s3_346 s3_466 s3_586;

EOF
) &


(
echo "[$server25]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server25} 'Xf4aGbTaf!' '/root' s3_61 s3_181 s3_301 s3_421 s3_541;

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server26} 'Xf4aGbTaf!' '/root' s3_3 s3_123 s3_243 s3_363 s3_483;

EOF
) &


(
echo "[$server27]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server27} '' '/root' s3_79 s3_199 s3_319 s3_439 s3_559;

EOF
) &


(
echo "[$server28]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server28} '' '/root' s3_108 s3_228 s3_348 s3_468 s3_588;

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server29} '' '/root' s3_15 s3_135 s3_255 s3_375 s3_495;

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server30} 'Xf4aGbTaf!' '/root' s3_44 s3_164 s3_284 s3_404 s3_524;

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server31} '' '/root' s3_22 s3_142 s3_262 s3_382 s3_502;

EOF
) &


(
echo "[$server32]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server32} 'Xf4aGbTaf!' '/root' s3_75 s3_195 s3_315 s3_435 s3_555;

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server33} 'Xf4aGbTaf!' '/root' s3_53 s3_173 s3_293 s3_413 s3_533;

EOF
) &


(
echo "[$server34]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server34} '' '/root' s3_74 s3_194 s3_314 s3_434 s3_554;

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server35} '' '/root' s3_55 s3_175 s3_295 s3_415 s3_535;

EOF
) &


(
echo "[$server36]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server36} 'Xf4aGbTaf!' '/root' s3_115 s3_235 s3_355 s3_475 s3_595;

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server37} 'Xf4aGbTaf!' '/root' s3_52 s3_172 s3_292 s3_412 s3_532;

EOF
) &


(
echo "[$server38]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server38} '' '/root' s3_62 s3_182 s3_302 s3_422 s3_542;

EOF
) &


(
echo "[$server39]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server39} '' '/root' s3_102 s3_222 s3_342 s3_462 s3_582;

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server40} '' '/root' s3_5 s3_125 s3_245 s3_365 s3_485;

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server41} 'Xf4aGbTaf!' '/root' s3_12 s3_132 s3_252 s3_372 s3_492;

EOF
) &


(
echo "[$server42]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server42} '' '/root' s3_94 s3_214 s3_334 s3_454 s3_574;

EOF
) &


(
echo "[$server43]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server43} 'Xf4aGbTaf!' '/root' s3_90 s3_210 s3_330 s3_450 s3_570;

EOF
) &


(
echo "[$server44]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server44} 'Xf4aGbTaf!' '/root' s3_70 s3_190 s3_310 s3_430 s3_550;

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server45} '' '/root' s3_40 s3_160 s3_280 s3_400 s3_520;

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server46} '' '/root' s3_41 s3_161 s3_281 s3_401 s3_521;

EOF
) &


(
echo "[$server47]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server47} 'Xf4aGbTaf!' '/root' s3_92 s3_212 s3_332 s3_452 s3_572;

EOF
) &


(
echo "[$server48]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server48} 'Xf4aGbTaf!' '/root' s3_86 s3_206 s3_326 s3_446 s3_566;

EOF
) &


(
echo "[$server49]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server49} '' '/root' s3_109 s3_229 s3_349 s3_469 s3_589;

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server50} '' '/root' s3_49 s3_169 s3_289 s3_409 s3_529;

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server51} '' '/root' s3_39 s3_159 s3_279 s3_399 s3_519;

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server52} 'Xf4aGbTaf!' '/root' r3 s3_120 s3_240 s3_360 s3_480 s3_600;

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server53} '' '/root' s3_48 s3_168 s3_288 s3_408 s3_528;

EOF
) &


(
echo "[$server54]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server54} 'Xf4aGbTaf!' '/root' s3_88 s3_208 s3_328 s3_448 s3_568;

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server55} 'Xf4aGbTaf!' '/root' s3_29 s3_149 s3_269 s3_389 s3_509;

EOF
) &


(
echo "[$server56]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server56} '' '/root' s3_64 s3_184 s3_304 s3_424 s3_544;

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server57} '' '/root' s3_31 s3_151 s3_271 s3_391 s3_511;

EOF
) &


(
echo "[$server58]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server58} 'Xf4aGbTaf!' '/root' s3_95 s3_215 s3_335 s3_455 s3_575;

EOF
) &


(
echo "[$server59]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server59} 'Xf4aGbTaf!' '/root' s3_68 s3_188 s3_308 s3_428 s3_548;

EOF
) &


(
echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server60} '' '/root' s3_50 s3_170 s3_290 s3_410 s3_530;

EOF
) &


(
echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server61} '' '/root' s3_43 s3_163 s3_283 s3_403 s3_523;

EOF
) &


(
echo "[$server62]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server62 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server62} '' '/root' s3_84 s3_204 s3_324 s3_444 s3_564;

EOF
) &


(
echo "[$server63]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server63 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server63} 'Xf4aGbTaf!' '/root' s3_67 s3_187 s3_307 s3_427 s3_547;

EOF
) &


(
echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server64} '' '/root' s3_37 s3_157 s3_277 s3_397 s3_517;

EOF
) &


(
echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server65} 'Xf4aGbTaf!' '/root' s3_24 s3_144 s3_264 s3_384 s3_504;

EOF
) &


(
echo "[$server66]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server66 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server66} 'Xf4aGbTaf!' '/root' s3_101 s3_221 s3_341 s3_461 s3_581;

EOF
) &


(
echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server67} '' '/root' s3_11 s3_131 s3_251 s3_371 s3_491;

EOF
) &


(
echo "[$server68]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server68 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server68} '' '/root' s3_117 s3_237 s3_357 s3_477 s3_597;

EOF
) &


(
echo "[$server69]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server69 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server69} 'Xf4aGbTaf!' '/root' s3_104 s3_224 s3_344 s3_464 s3_584;

EOF
) &


(
echo "[$server70]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server70 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server70} 'Xf4aGbTaf!' '/root' s3_107 s3_227 s3_347 s3_467 s3_587;

EOF
) &


(
echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server71} '' '/root' s3_32 s3_152 s3_272 s3_392 s3_512;

EOF
) &


(
echo "[$server72]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server72 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server72} '' '/root' s3_63 s3_183 s3_303 s3_423 s3_543;

EOF
) &


(
echo "[$server73]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server73 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server73} '' '/root' s3_112 s3_232 s3_352 s3_472 s3_592;

EOF
) &


(
echo "[$server74]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server74 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server74} 'Xf4aGbTaf!' '/root' s3_113 s3_233 s3_353 s3_473 s3_593;

EOF
) &


(
echo "[$server75]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server75 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server75} '' '/root' s3_77 s3_197 s3_317 s3_437 s3_557;

EOF
) &


(
echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server76} 'Xf4aGbTaf!' '/root' s3_14 s3_134 s3_254 s3_374 s3_494;

EOF
) &


(
echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server77} 'Xf4aGbTaf!' '/root' s3_25 s3_145 s3_265 s3_385 s3_505;

EOF
) &


(
echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server78} '' '/root' s3_38 s3_158 s3_278 s3_398 s3_518;

EOF
) &


(
echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server79} '' '/root' s3_51 s3_171 s3_291 s3_411 s3_531;

EOF
) &


(
echo "[$server80]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server80 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server80} 'Xf4aGbTaf!' '/root' s3_91 s3_211 s3_331 s3_451 s3_571;

EOF
) &


(
echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server81} 'Xf4aGbTaf!' '/root' s3_46 s3_166 s3_286 s3_406 s3_526;

EOF
) &


(
echo "[$server82]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server82 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server82} '' '/root' s3_60 s3_180 s3_300 s3_420 s3_540;

EOF
) &


(
echo "[$server83]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server83 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server83} '' '/root' s3_81 s3_201 s3_321 s3_441 s3_561;

EOF
) &


(
echo "[$server84]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server84 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server84} '' '/root' s3_80 s3_200 s3_320 s3_440 s3_560;

EOF
) &


(
echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server85} 'Xf4aGbTaf!' '/root' s3_45 s3_165 s3_285 s3_405 s3_525;

EOF
) &


(
echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server86} '' '/root' s3_33 s3_153 s3_273 s3_393 s3_513;

EOF
) &


(
echo "[$server87]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server87 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server87} 'Xf4aGbTaf!' '/root' s3_105 s3_225 s3_345 s3_465 s3_585;

EOF
) &


(
echo "[$server88]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server88 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server88} 'Xf4aGbTaf!' '/root' s3_71 s3_191 s3_311 s3_431 s3_551;

EOF
) &


(
echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server89} '' '/root' s3_56 s3_176 s3_296 s3_416 s3_536;

EOF
) &


(
echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server90} '' '/root' s3_23 s3_143 s3_263 s3_383 s3_503;

EOF
) &


(
echo "[$server91]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server91 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server91} 'Xf4aGbTaf!' '/root' s3_69 s3_189 s3_309 s3_429 s3_549;

EOF
) &


(
echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server92} 'Xf4aGbTaf!' '/root' s3_26 s3_146 s3_266 s3_386 s3_506;

EOF
) &


(
echo "[$server93]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server93 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server93} '' '/root' s3_114 s3_234 s3_354 s3_474 s3_594;

EOF
) &


(
echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server94} '' '/root' s3_19 s3_139 s3_259 s3_379 s3_499;

EOF
) &


(
echo "[$server95]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server95 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server95} '' '/root' s3_66 s3_186 s3_306 s3_426 s3_546;

EOF
) &


(
echo "[$server96]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server96 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server96} 'Xf4aGbTaf!' '/root' s3_72 s3_192 s3_312 s3_432 s3_552;

EOF
) &


(
echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server97} '' '/root' s3_57 s3_177 s3_297 s3_417 s3_537;

EOF
) &


(
echo "[$server98]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server98 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server98} 'Xf4aGbTaf!' '/root' s3_100 s3_220 s3_340 s3_460 s3_580;

EOF
) &


(
echo "[$server99]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server99 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server99} 'Xf4aGbTaf!' '/root' s3_99 s3_219 s3_339 s3_459 s3_579;

EOF
) &


(
echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server100} '' '/root' s3_47 s3_167 s3_287 s3_407 s3_527;

EOF
) &


(
echo "[$server101]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server101 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server101} '' '/root' s3_76 s3_196 s3_316 s3_436 s3_556;

EOF
) &


(
echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server102} 'Xf4aGbTaf!' '/root' s3_8 s3_128 s3_248 s3_368 s3_488;

EOF
) &


(
echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server103} 'Xf4aGbTaf!' '/root' s3_17 s3_137 s3_257 s3_377 s3_497;

EOF
) &


(
echo "[$server104]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server104 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server104} '' '/root' s3_59 s3_179 s3_299 s3_419 s3_539;

EOF
) &


(
echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server105} '' '/root' s3_30 s3_150 s3_270 s3_390 s3_510;

EOF
) &


(
echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server106} '' '/root' s3_28 s3_148 s3_268 s3_388 s3_508;

EOF
) &


(
echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server107} 'Xf4aGbTaf!' '/root' s3_1 s3_121 s3_241 s3_361 s3_481;

EOF
) &


(
echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server108} '' '/root' s3_27 s3_147 s3_267 s3_387 s3_507;

EOF
) &


(
echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server11} ${server109} 'Xf4aGbTaf!' '/root' s3_21 s3_141 s3_261 s3_381 s3_501;

EOF
) &


(
echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server1} ${server110} 'Xf4aGbTaf!' '/root' s3_6 s3_126 s3_246 s3_366 s3_486;

EOF
) &


(
echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server2} ${server111} '' '/root' s3_7 s3_127 s3_247 s3_367 s3_487;

EOF
) &


(
echo "[$server112]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server112 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server3} ${server112} '' '/root' s3_116 s3_236 s3_356 s3_476 s3_596;

EOF
) &


(
echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server4} ${server113} 'Xf4aGbTaf!' '/root' s3_36 s3_156 s3_276 s3_396 s3_516;

EOF
) &


(
echo "[$server114]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server114 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server5} ${server114} 'Xf4aGbTaf!' '/root' s3_82 s3_202 s3_322 s3_442 s3_562;

EOF
) &


(
echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server6} ${server115} '' '/root' s3_10 s3_130 s3_250 s3_370 s3_490;

EOF
) &


(
echo "[$server116]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server116 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server7} ${server116} '' '/root' s3_73 s3_193 s3_313 s3_433 s3_553;

EOF
) &


(
echo "[$server117]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server117 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server8} ${server117} '' '/root' s3_85 s3_205 s3_325 s3_445 s3_565;

EOF
) &


(
echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server9} ${server118} 'Xf4aGbTaf!' '/root' s3_9 s3_129 s3_249 s3_369 s3_489;

EOF
) &


(
echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<EOF
mkdir -p /root;
rm -rf /root/zjnodes;
sshpass -p 'Xf4aGbTaf!' scp -o StrictHostKeyChecking=no root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server10} ${server119} '' '/root' s3_42 s3_162 s3_282 s3_402 s3_522;

EOF
) &

wait

(
echo "[$server0]"
for n in r1 s3_118 s3_238 s3_358 s3_478 s3_598; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/${n}
done
) &


(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server1 <<EOF
for n in s3_16 s3_136 s3_256 s3_376 s3_496; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_16 s3_136 s3_256 s3_376 s3_496; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server2]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server2 <<EOF
for n in s3_89 s3_209 s3_329 s3_449 s3_569; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_89 s3_209 s3_329 s3_449 s3_569; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server3]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server3 <<EOF
for n in s3_97 s3_217 s3_337 s3_457 s3_577; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_97 s3_217 s3_337 s3_457 s3_577; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server4 <<EOF
for n in s3_4 s3_124 s3_244 s3_364 s3_484; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_4 s3_124 s3_244 s3_364 s3_484; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server5 <<EOF
for n in s3_18 s3_138 s3_258 s3_378 s3_498; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_18 s3_138 s3_258 s3_378 s3_498; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server6]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server6 <<EOF
for n in s3_78 s3_198 s3_318 s3_438 s3_558; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_78 s3_198 s3_318 s3_438 s3_558; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server7]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server7 <<EOF
for n in s3_110 s3_230 s3_350 s3_470 s3_590; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_110 s3_230 s3_350 s3_470 s3_590; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server8]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server8 <<EOF
for n in s3_87 s3_207 s3_327 s3_447 s3_567; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_87 s3_207 s3_327 s3_447 s3_567; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<EOF
for n in s3_54 s3_174 s3_294 s3_414 s3_534; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_54 s3_174 s3_294 s3_414 s3_534; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server10]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server10 <<EOF
for n in s3_98 s3_218 s3_338 s3_458 s3_578; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_98 s3_218 s3_338 s3_458 s3_578; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server11 <<EOF
for n in r2 s3_119 s3_239 s3_359 s3_479 s3_599; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r2; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db &
done
wait

for n in s3_119 s3_239 s3_359 s3_479 s3_599; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server12 <<EOF
for n in s3_35 s3_155 s3_275 s3_395 s3_515; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_35 s3_155 s3_275 s3_395 s3_515; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server13]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server13 <<EOF
for n in s3_65 s3_185 s3_305 s3_425 s3_545; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_65 s3_185 s3_305 s3_425 s3_545; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server14]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server14 <<EOF
for n in s3_93 s3_213 s3_333 s3_453 s3_573; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_93 s3_213 s3_333 s3_453 s3_573; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server15]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server15 <<EOF
for n in s3_58 s3_178 s3_298 s3_418 s3_538; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_58 s3_178 s3_298 s3_418 s3_538; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server16 <<EOF
for n in s3_2 s3_122 s3_242 s3_362 s3_482; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_2 s3_122 s3_242 s3_362 s3_482; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server17 <<EOF
for n in s3_13 s3_133 s3_253 s3_373 s3_493; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_13 s3_133 s3_253 s3_373 s3_493; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server18]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server18 <<EOF
for n in s3_96 s3_216 s3_336 s3_456 s3_576; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_96 s3_216 s3_336 s3_456 s3_576; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server19]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server19 <<EOF
for n in s3_103 s3_223 s3_343 s3_463 s3_583; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_103 s3_223 s3_343 s3_463 s3_583; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server20]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server20 <<EOF
for n in s3_83 s3_203 s3_323 s3_443 s3_563; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_83 s3_203 s3_323 s3_443 s3_563; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server21]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server21 <<EOF
for n in s3_111 s3_231 s3_351 s3_471 s3_591; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_111 s3_231 s3_351 s3_471 s3_591; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server22 <<EOF
for n in s3_20 s3_140 s3_260 s3_380 s3_500; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_20 s3_140 s3_260 s3_380 s3_500; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server23 <<EOF
for n in s3_34 s3_154 s3_274 s3_394 s3_514; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_34 s3_154 s3_274 s3_394 s3_514; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server24]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server24 <<EOF
for n in s3_106 s3_226 s3_346 s3_466 s3_586; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_106 s3_226 s3_346 s3_466 s3_586; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server25]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server25 <<EOF
for n in s3_61 s3_181 s3_301 s3_421 s3_541; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_61 s3_181 s3_301 s3_421 s3_541; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<EOF
for n in s3_3 s3_123 s3_243 s3_363 s3_483; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_3 s3_123 s3_243 s3_363 s3_483; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server27]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server27 <<EOF
for n in s3_79 s3_199 s3_319 s3_439 s3_559; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_79 s3_199 s3_319 s3_439 s3_559; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server28]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server28 <<EOF
for n in s3_108 s3_228 s3_348 s3_468 s3_588; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_108 s3_228 s3_348 s3_468 s3_588; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server29 <<EOF
for n in s3_15 s3_135 s3_255 s3_375 s3_495; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_15 s3_135 s3_255 s3_375 s3_495; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server30 <<EOF
for n in s3_44 s3_164 s3_284 s3_404 s3_524; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_44 s3_164 s3_284 s3_404 s3_524; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server31 <<EOF
for n in s3_22 s3_142 s3_262 s3_382 s3_502; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_22 s3_142 s3_262 s3_382 s3_502; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server32]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server32 <<EOF
for n in s3_75 s3_195 s3_315 s3_435 s3_555; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_75 s3_195 s3_315 s3_435 s3_555; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<EOF
for n in s3_53 s3_173 s3_293 s3_413 s3_533; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_53 s3_173 s3_293 s3_413 s3_533; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server34]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server34 <<EOF
for n in s3_74 s3_194 s3_314 s3_434 s3_554; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_74 s3_194 s3_314 s3_434 s3_554; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<EOF
for n in s3_55 s3_175 s3_295 s3_415 s3_535; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_55 s3_175 s3_295 s3_415 s3_535; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server36]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server36 <<EOF
for n in s3_115 s3_235 s3_355 s3_475 s3_595; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_115 s3_235 s3_355 s3_475 s3_595; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<EOF
for n in s3_52 s3_172 s3_292 s3_412 s3_532; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_52 s3_172 s3_292 s3_412 s3_532; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server38]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server38 <<EOF
for n in s3_62 s3_182 s3_302 s3_422 s3_542; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_62 s3_182 s3_302 s3_422 s3_542; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server39]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server39 <<EOF
for n in s3_102 s3_222 s3_342 s3_462 s3_582; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_102 s3_222 s3_342 s3_462 s3_582; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server40 <<EOF
for n in s3_5 s3_125 s3_245 s3_365 s3_485; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_5 s3_125 s3_245 s3_365 s3_485; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server41 <<EOF
for n in s3_12 s3_132 s3_252 s3_372 s3_492; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_12 s3_132 s3_252 s3_372 s3_492; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server42]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server42 <<EOF
for n in s3_94 s3_214 s3_334 s3_454 s3_574; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_94 s3_214 s3_334 s3_454 s3_574; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server43]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server43 <<EOF
for n in s3_90 s3_210 s3_330 s3_450 s3_570; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_90 s3_210 s3_330 s3_450 s3_570; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server44]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server44 <<EOF
for n in s3_70 s3_190 s3_310 s3_430 s3_550; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_70 s3_190 s3_310 s3_430 s3_550; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server45 <<EOF
for n in s3_40 s3_160 s3_280 s3_400 s3_520; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_40 s3_160 s3_280 s3_400 s3_520; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server46 <<EOF
for n in s3_41 s3_161 s3_281 s3_401 s3_521; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_41 s3_161 s3_281 s3_401 s3_521; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server47]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server47 <<EOF
for n in s3_92 s3_212 s3_332 s3_452 s3_572; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_92 s3_212 s3_332 s3_452 s3_572; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server48]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server48 <<EOF
for n in s3_86 s3_206 s3_326 s3_446 s3_566; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_86 s3_206 s3_326 s3_446 s3_566; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server49]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server49 <<EOF
for n in s3_109 s3_229 s3_349 s3_469 s3_589; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_109 s3_229 s3_349 s3_469 s3_589; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server50 <<EOF
for n in s3_49 s3_169 s3_289 s3_409 s3_529; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_49 s3_169 s3_289 s3_409 s3_529; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server51 <<EOF
for n in s3_39 s3_159 s3_279 s3_399 s3_519; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_39 s3_159 s3_279 s3_399 s3_519; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server52 <<EOF
for n in r3 s3_120 s3_240 s3_360 s3_480 s3_600; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in r3; do
    cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/\${n}/db &
done
wait

for n in s3_120 s3_240 s3_360 s3_480 s3_600; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server53 <<EOF
for n in s3_48 s3_168 s3_288 s3_408 s3_528; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_48 s3_168 s3_288 s3_408 s3_528; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server54]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server54 <<EOF
for n in s3_88 s3_208 s3_328 s3_448 s3_568; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_88 s3_208 s3_328 s3_448 s3_568; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<EOF
for n in s3_29 s3_149 s3_269 s3_389 s3_509; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_29 s3_149 s3_269 s3_389 s3_509; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server56]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server56 <<EOF
for n in s3_64 s3_184 s3_304 s3_424 s3_544; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_64 s3_184 s3_304 s3_424 s3_544; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<EOF
for n in s3_31 s3_151 s3_271 s3_391 s3_511; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_31 s3_151 s3_271 s3_391 s3_511; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server58]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server58 <<EOF
for n in s3_95 s3_215 s3_335 s3_455 s3_575; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_95 s3_215 s3_335 s3_455 s3_575; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server59]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server59 <<EOF
for n in s3_68 s3_188 s3_308 s3_428 s3_548; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_68 s3_188 s3_308 s3_428 s3_548; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<EOF
for n in s3_50 s3_170 s3_290 s3_410 s3_530; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_50 s3_170 s3_290 s3_410 s3_530; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<EOF
for n in s3_43 s3_163 s3_283 s3_403 s3_523; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_43 s3_163 s3_283 s3_403 s3_523; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server62]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server62 <<EOF
for n in s3_84 s3_204 s3_324 s3_444 s3_564; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_84 s3_204 s3_324 s3_444 s3_564; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server63]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server63 <<EOF
for n in s3_67 s3_187 s3_307 s3_427 s3_547; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_67 s3_187 s3_307 s3_427 s3_547; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<EOF
for n in s3_37 s3_157 s3_277 s3_397 s3_517; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_37 s3_157 s3_277 s3_397 s3_517; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<EOF
for n in s3_24 s3_144 s3_264 s3_384 s3_504; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_24 s3_144 s3_264 s3_384 s3_504; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server66]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server66 <<EOF
for n in s3_101 s3_221 s3_341 s3_461 s3_581; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_101 s3_221 s3_341 s3_461 s3_581; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<EOF
for n in s3_11 s3_131 s3_251 s3_371 s3_491; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_11 s3_131 s3_251 s3_371 s3_491; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server68]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server68 <<EOF
for n in s3_117 s3_237 s3_357 s3_477 s3_597; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_117 s3_237 s3_357 s3_477 s3_597; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server69]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server69 <<EOF
for n in s3_104 s3_224 s3_344 s3_464 s3_584; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_104 s3_224 s3_344 s3_464 s3_584; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server70]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server70 <<EOF
for n in s3_107 s3_227 s3_347 s3_467 s3_587; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_107 s3_227 s3_347 s3_467 s3_587; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<EOF
for n in s3_32 s3_152 s3_272 s3_392 s3_512; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_32 s3_152 s3_272 s3_392 s3_512; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server72]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server72 <<EOF
for n in s3_63 s3_183 s3_303 s3_423 s3_543; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_63 s3_183 s3_303 s3_423 s3_543; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server73]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server73 <<EOF
for n in s3_112 s3_232 s3_352 s3_472 s3_592; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_112 s3_232 s3_352 s3_472 s3_592; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server74]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server74 <<EOF
for n in s3_113 s3_233 s3_353 s3_473 s3_593; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_113 s3_233 s3_353 s3_473 s3_593; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server75]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server75 <<EOF
for n in s3_77 s3_197 s3_317 s3_437 s3_557; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_77 s3_197 s3_317 s3_437 s3_557; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<EOF
for n in s3_14 s3_134 s3_254 s3_374 s3_494; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_14 s3_134 s3_254 s3_374 s3_494; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<EOF
for n in s3_25 s3_145 s3_265 s3_385 s3_505; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_25 s3_145 s3_265 s3_385 s3_505; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<EOF
for n in s3_38 s3_158 s3_278 s3_398 s3_518; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_38 s3_158 s3_278 s3_398 s3_518; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<EOF
for n in s3_51 s3_171 s3_291 s3_411 s3_531; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_51 s3_171 s3_291 s3_411 s3_531; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server80]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server80 <<EOF
for n in s3_91 s3_211 s3_331 s3_451 s3_571; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_91 s3_211 s3_331 s3_451 s3_571; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<EOF
for n in s3_46 s3_166 s3_286 s3_406 s3_526; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_46 s3_166 s3_286 s3_406 s3_526; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server82]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server82 <<EOF
for n in s3_60 s3_180 s3_300 s3_420 s3_540; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_60 s3_180 s3_300 s3_420 s3_540; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server83]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server83 <<EOF
for n in s3_81 s3_201 s3_321 s3_441 s3_561; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_81 s3_201 s3_321 s3_441 s3_561; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server84]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server84 <<EOF
for n in s3_80 s3_200 s3_320 s3_440 s3_560; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_80 s3_200 s3_320 s3_440 s3_560; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<EOF
for n in s3_45 s3_165 s3_285 s3_405 s3_525; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_45 s3_165 s3_285 s3_405 s3_525; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<EOF
for n in s3_33 s3_153 s3_273 s3_393 s3_513; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_33 s3_153 s3_273 s3_393 s3_513; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server87]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server87 <<EOF
for n in s3_105 s3_225 s3_345 s3_465 s3_585; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_105 s3_225 s3_345 s3_465 s3_585; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server88]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server88 <<EOF
for n in s3_71 s3_191 s3_311 s3_431 s3_551; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_71 s3_191 s3_311 s3_431 s3_551; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<EOF
for n in s3_56 s3_176 s3_296 s3_416 s3_536; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_56 s3_176 s3_296 s3_416 s3_536; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<EOF
for n in s3_23 s3_143 s3_263 s3_383 s3_503; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_23 s3_143 s3_263 s3_383 s3_503; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server91]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server91 <<EOF
for n in s3_69 s3_189 s3_309 s3_429 s3_549; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_69 s3_189 s3_309 s3_429 s3_549; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<EOF
for n in s3_26 s3_146 s3_266 s3_386 s3_506; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_26 s3_146 s3_266 s3_386 s3_506; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server93]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server93 <<EOF
for n in s3_114 s3_234 s3_354 s3_474 s3_594; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_114 s3_234 s3_354 s3_474 s3_594; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<EOF
for n in s3_19 s3_139 s3_259 s3_379 s3_499; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_19 s3_139 s3_259 s3_379 s3_499; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server95]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server95 <<EOF
for n in s3_66 s3_186 s3_306 s3_426 s3_546; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_66 s3_186 s3_306 s3_426 s3_546; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server96]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server96 <<EOF
for n in s3_72 s3_192 s3_312 s3_432 s3_552; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_72 s3_192 s3_312 s3_432 s3_552; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<EOF
for n in s3_57 s3_177 s3_297 s3_417 s3_537; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_57 s3_177 s3_297 s3_417 s3_537; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server98]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server98 <<EOF
for n in s3_100 s3_220 s3_340 s3_460 s3_580; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_100 s3_220 s3_340 s3_460 s3_580; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server99]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server99 <<EOF
for n in s3_99 s3_219 s3_339 s3_459 s3_579; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_99 s3_219 s3_339 s3_459 s3_579; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<EOF
for n in s3_47 s3_167 s3_287 s3_407 s3_527; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_47 s3_167 s3_287 s3_407 s3_527; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server101]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server101 <<EOF
for n in s3_76 s3_196 s3_316 s3_436 s3_556; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_76 s3_196 s3_316 s3_436 s3_556; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<EOF
for n in s3_8 s3_128 s3_248 s3_368 s3_488; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_8 s3_128 s3_248 s3_368 s3_488; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<EOF
for n in s3_17 s3_137 s3_257 s3_377 s3_497; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_17 s3_137 s3_257 s3_377 s3_497; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server104]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server104 <<EOF
for n in s3_59 s3_179 s3_299 s3_419 s3_539; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_59 s3_179 s3_299 s3_419 s3_539; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<EOF
for n in s3_30 s3_150 s3_270 s3_390 s3_510; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_30 s3_150 s3_270 s3_390 s3_510; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<EOF
for n in s3_28 s3_148 s3_268 s3_388 s3_508; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_28 s3_148 s3_268 s3_388 s3_508; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<EOF
for n in s3_1 s3_121 s3_241 s3_361 s3_481; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_1 s3_121 s3_241 s3_361 s3_481; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<EOF
for n in s3_27 s3_147 s3_267 s3_387 s3_507; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_27 s3_147 s3_267 s3_387 s3_507; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<EOF
for n in s3_21 s3_141 s3_261 s3_381 s3_501; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_21 s3_141 s3_261 s3_381 s3_501; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<EOF
for n in s3_6 s3_126 s3_246 s3_366 s3_486; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_6 s3_126 s3_246 s3_366 s3_486; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<EOF
for n in s3_7 s3_127 s3_247 s3_367 s3_487; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_7 s3_127 s3_247 s3_367 s3_487; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server112]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server112 <<EOF
for n in s3_116 s3_236 s3_356 s3_476 s3_596; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_116 s3_236 s3_356 s3_476 s3_596; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<EOF
for n in s3_36 s3_156 s3_276 s3_396 s3_516; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_36 s3_156 s3_276 s3_396 s3_516; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server114]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server114 <<EOF
for n in s3_82 s3_202 s3_322 s3_442 s3_562; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_82 s3_202 s3_322 s3_442 s3_562; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<EOF
for n in s3_10 s3_130 s3_250 s3_370 s3_490; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_10 s3_130 s3_250 s3_370 s3_490; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server116]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server116 <<EOF
for n in s3_73 s3_193 s3_313 s3_433 s3_553; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_73 s3_193 s3_313 s3_433 s3_553; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server117]"
sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server117 <<EOF
for n in s3_85 s3_205 s3_325 s3_445 s3_565; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_85 s3_205 s3_325 s3_445 s3_565; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<EOF
for n in s3_9 s3_129 s3_249 s3_369 s3_489; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_9 s3_129 s3_249 s3_369 s3_489; do
    cp -rf /root/zjnodes/zjchain/shard_db_3 /root/zjnodes/\${n}/db &
done
wait

EOF
) &


(
echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<EOF
for n in s3_42 s3_162 s3_282 s3_402 s3_522; do
    ln -s /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/\${n}/conf
    ln -s /root/zjnodes/zjchain/zjchain /root/zjnodes/\${n}
done

for n in s3_42 s3_162 s3_282 s3_402 s3_522; do
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

for n in s3_118 s3_238 s3_358 s3_478 s3_598; do
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server2 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server3]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server3 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server6 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server7]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server7 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server8]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server8 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server9]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server9 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server10]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server10 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server13 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server14]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server14 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server15]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server15 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server18 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server19]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server19 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server20]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server20 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server21]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server21 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server24 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server25]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server25 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server26]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server26 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server27]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server27 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server28]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server28 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server32 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server33]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server33 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server34]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server34 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server35]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server35 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server36]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server36 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server37]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server37 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server38]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server38 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server39]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server39 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server42 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server43]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server43 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server44]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server44 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server47 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server48]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server48 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server49]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server49 <<"EOF"
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
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server54 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server55]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server55 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server56]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server56 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server57]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server57 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server58]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server58 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server59]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server59 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server60]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server60 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server61]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server61 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server62]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server62 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server63]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server63 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server64]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server64 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server65]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server65 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server66]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server66 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server67]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server67 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server68]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server68 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server69]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server69 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server70]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server70 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server71]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server71 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server72]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server72 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server73]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server73 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server74]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server74 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server75]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server75 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server76]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server76 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server77]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server77 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server78]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server78 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server79]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server79 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server80]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server80 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server81]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server81 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server82]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server82 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server83]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server83 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server84]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server84 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server85]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server85 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server86]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server86 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server87]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server87 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server88]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server88 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server89]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server89 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server90]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server90 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server91]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server91 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server92]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server92 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server93]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server93 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server94]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server94 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server95]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server95 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server96]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server96 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server97]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server97 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server98]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server98 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server99]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server99 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server100]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server100 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server101]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server101 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server102]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server102 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server103]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server103 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server104]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server104 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server105]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server105 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server106]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server106 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server107]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server107 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server108]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server108 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server109]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server109 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server110]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server110 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server111]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server111 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server112]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server112 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server113]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server113 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server114]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server114 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server115]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server115 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server116]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server116 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server117]"
# sshpass -p '' ssh -o StrictHostKeyChecking=no root@$server117 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server118]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server118 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "[$server119]"
# sshpass -p 'Xf4aGbTaf!' ssh -o StrictHostKeyChecking=no root@$server119 <<"EOF"
# ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
# EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 root> /dev/null 2>&1 &

sleep 3

(
echo "[$server1]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_16 s3_136 s3_256 s3_376 s3_496; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server2]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_89 s3_209 s3_329 s3_449 s3_569; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server3]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server3 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_97 s3_217 s3_337 s3_457 s3_577; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server4]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server4 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_4 s3_124 s3_244 s3_364 s3_484; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server5]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server5 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_18 s3_138 s3_258 s3_378 s3_498; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server6]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server6 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_78 s3_198 s3_318 s3_438 s3_558; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server7]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server7 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_110 s3_230 s3_350 s3_470 s3_590; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server8]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server8 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_87 s3_207 s3_327 s3_447 s3_567; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server9]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server9 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_54 s3_174 s3_294 s3_414 s3_534; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server10]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server10 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_98 s3_218 s3_338 s3_458 s3_578; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server11]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server11 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_119 s3_239 s3_359 s3_479 s3_599; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server12]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server12 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_35 s3_155 s3_275 s3_395 s3_515; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server13]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server13 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_65 s3_185 s3_305 s3_425 s3_545; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server14]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server14 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_93 s3_213 s3_333 s3_453 s3_573; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server15]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server15 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_58 s3_178 s3_298 s3_418 s3_538; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server16]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server16 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_2 s3_122 s3_242 s3_362 s3_482; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server17]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server17 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_13 s3_133 s3_253 s3_373 s3_493; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server18]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server18 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_96 s3_216 s3_336 s3_456 s3_576; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server19]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server19 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_103 s3_223 s3_343 s3_463 s3_583; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server20]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server20 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_83 s3_203 s3_323 s3_443 s3_563; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server21]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server21 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_111 s3_231 s3_351 s3_471 s3_591; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server22]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server22 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_20 s3_140 s3_260 s3_380 s3_500; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server23]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server23 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_34 s3_154 s3_274 s3_394 s3_514; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server24]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server24 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_106 s3_226 s3_346 s3_466 s3_586; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server25]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server25 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_61 s3_181 s3_301 s3_421 s3_541; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server26]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server26 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_3 s3_123 s3_243 s3_363 s3_483; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server27]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server27 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_79 s3_199 s3_319 s3_439 s3_559; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server28]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server28 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_108 s3_228 s3_348 s3_468 s3_588; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server29]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server29 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_15 s3_135 s3_255 s3_375 s3_495; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server30]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server30 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_44 s3_164 s3_284 s3_404 s3_524; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server31]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server31 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_22 s3_142 s3_262 s3_382 s3_502; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server32]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server32 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_75 s3_195 s3_315 s3_435 s3_555; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server33]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server33 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_53 s3_173 s3_293 s3_413 s3_533; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server34]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server34 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_74 s3_194 s3_314 s3_434 s3_554; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server35]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server35 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_55 s3_175 s3_295 s3_415 s3_535; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server36]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server36 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_115 s3_235 s3_355 s3_475 s3_595; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server37]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server37 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_52 s3_172 s3_292 s3_412 s3_532; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server38]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server38 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_62 s3_182 s3_302 s3_422 s3_542; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server39]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server39 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_102 s3_222 s3_342 s3_462 s3_582; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server40]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server40 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_5 s3_125 s3_245 s3_365 s3_485; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server41]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server41 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_12 s3_132 s3_252 s3_372 s3_492; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server42]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server42 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_94 s3_214 s3_334 s3_454 s3_574; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server43]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server43 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_90 s3_210 s3_330 s3_450 s3_570; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server44]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server44 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_70 s3_190 s3_310 s3_430 s3_550; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server45]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server45 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_40 s3_160 s3_280 s3_400 s3_520; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server46]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server46 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_41 s3_161 s3_281 s3_401 s3_521; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server47]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server47 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_92 s3_212 s3_332 s3_452 s3_572; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server48]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server48 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_86 s3_206 s3_326 s3_446 s3_566; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server49]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server49 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_109 s3_229 s3_349 s3_469 s3_589; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server50]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server50 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_49 s3_169 s3_289 s3_409 s3_529; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server51]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server51 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_39 s3_159 s3_279 s3_399 s3_519; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server52]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server52 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_120 s3_240 s3_360 s3_480 s3_600; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server53]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server53 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_48 s3_168 s3_288 s3_408 s3_528; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server54]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server54 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_88 s3_208 s3_328 s3_448 s3_568; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server55]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server55 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_29 s3_149 s3_269 s3_389 s3_509; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server56]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server56 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_64 s3_184 s3_304 s3_424 s3_544; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server57]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server57 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_31 s3_151 s3_271 s3_391 s3_511; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server58]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server58 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_95 s3_215 s3_335 s3_455 s3_575; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server59]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server59 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_68 s3_188 s3_308 s3_428 s3_548; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server60]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server60 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_50 s3_170 s3_290 s3_410 s3_530; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server61]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server61 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_43 s3_163 s3_283 s3_403 s3_523; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server62]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server62 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_84 s3_204 s3_324 s3_444 s3_564; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server63]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server63 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_67 s3_187 s3_307 s3_427 s3_547; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server64]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server64 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_37 s3_157 s3_277 s3_397 s3_517; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server65]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server65 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_24 s3_144 s3_264 s3_384 s3_504; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server66]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server66 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_101 s3_221 s3_341 s3_461 s3_581; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server67]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server67 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_11 s3_131 s3_251 s3_371 s3_491; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server68]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server68 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_117 s3_237 s3_357 s3_477 s3_597; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server69]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server69 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_104 s3_224 s3_344 s3_464 s3_584; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server70]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server70 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_107 s3_227 s3_347 s3_467 s3_587; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server71]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server71 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_32 s3_152 s3_272 s3_392 s3_512; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server72]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server72 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_63 s3_183 s3_303 s3_423 s3_543; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server73]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server73 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_112 s3_232 s3_352 s3_472 s3_592; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server74]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server74 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_113 s3_233 s3_353 s3_473 s3_593; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server75]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server75 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_77 s3_197 s3_317 s3_437 s3_557; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server76]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server76 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_14 s3_134 s3_254 s3_374 s3_494; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server77]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server77 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_25 s3_145 s3_265 s3_385 s3_505; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server78]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server78 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_38 s3_158 s3_278 s3_398 s3_518; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server79]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server79 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_51 s3_171 s3_291 s3_411 s3_531; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server80]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server80 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_91 s3_211 s3_331 s3_451 s3_571; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server81]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server81 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_46 s3_166 s3_286 s3_406 s3_526; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server82]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server82 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_60 s3_180 s3_300 s3_420 s3_540; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server83]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server83 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_81 s3_201 s3_321 s3_441 s3_561; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server84]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server84 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_80 s3_200 s3_320 s3_440 s3_560; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server85]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server85 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_45 s3_165 s3_285 s3_405 s3_525; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server86]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server86 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_33 s3_153 s3_273 s3_393 s3_513; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server87]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server87 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_105 s3_225 s3_345 s3_465 s3_585; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server88]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server88 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_71 s3_191 s3_311 s3_431 s3_551; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server89]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server89 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_56 s3_176 s3_296 s3_416 s3_536; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server90]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server90 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_23 s3_143 s3_263 s3_383 s3_503; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server91]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server91 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_69 s3_189 s3_309 s3_429 s3_549; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server92]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server92 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_26 s3_146 s3_266 s3_386 s3_506; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server93]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server93 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_114 s3_234 s3_354 s3_474 s3_594; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server94]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server94 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_19 s3_139 s3_259 s3_379 s3_499; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server95]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server95 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_66 s3_186 s3_306 s3_426 s3_546; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server96]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server96 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_72 s3_192 s3_312 s3_432 s3_552; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server97]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server97 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_57 s3_177 s3_297 s3_417 s3_537; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server98]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server98 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_100 s3_220 s3_340 s3_460 s3_580; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server99]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server99 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_99 s3_219 s3_339 s3_459 s3_579; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server100]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server100 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_47 s3_167 s3_287 s3_407 s3_527; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server101]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server101 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_76 s3_196 s3_316 s3_436 s3_556; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server102]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server102 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_8 s3_128 s3_248 s3_368 s3_488; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server103]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server103 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_17 s3_137 s3_257 s3_377 s3_497; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server104]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server104 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_59 s3_179 s3_299 s3_419 s3_539; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server105]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server105 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_30 s3_150 s3_270 s3_390 s3_510; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server106]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server106 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_28 s3_148 s3_268 s3_388 s3_508; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server107]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server107 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_1 s3_121 s3_241 s3_361 s3_481; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server108]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server108 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_27 s3_147 s3_267 s3_387 s3_507; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server109]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server109 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_21 s3_141 s3_261 s3_381 s3_501; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server110]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server110 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s3_126 s3_246 s3_366 s3_486; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server111]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server111 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_7 s3_127 s3_247 s3_367 s3_487; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server112]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server112 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_116 s3_236 s3_356 s3_476 s3_596; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server113]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server113 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_36 s3_156 s3_276 s3_396 s3_516; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server114]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server114 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_82 s3_202 s3_322 s3_442 s3_562; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server115]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server115 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_10 s3_130 s3_250 s3_370 s3_490; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server116]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server116 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_73 s3_193 s3_313 s3_433 s3_553; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server117]"
sshpass -p '' ssh -f -o StrictHostKeyChecking=no root@$server117 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_85 s3_205 s3_325 s3_445 s3_565; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server118]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server118 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_9 s3_129 s3_249 s3_369 s3_489; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &

(
echo "[$server119]"
sshpass -p 'Xf4aGbTaf!' ssh -f -o StrictHostKeyChecking=no root@$server119 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_42 s3_162 s3_282 s3_402 s3_522; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node root> /dev/null 2>&1 &\
done \
'"
) &
wait

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in s3_118 s3_238 s3_358 s3_478 s3_598; do
cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node root> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
