
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
server1=10.101.20.33
pass=!@#$%^

echo "[$server0]"
sshpass -p $pass ssh root@$server0 <<EOF
cd /root/xufei/zjchain && sh deploy_genesis.sh Debug ${server0}
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} $pass r1 r2 s1 s2 s3 s6 s7 s8
EOF


echo "[$server1]"
sshpass -p $pass ssh root@$server1 <<EOF
sshpass -p $pass scp root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} $pass r3 s4 s5 s9 s10 s11
EOF


echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="


echo "[$server0]"
sshpass -p $pass ssh root@$server0 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server1]"
sshpass -p $pass ssh root@$server1 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
sshpass -p $pass ssh -f root@$server0 "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 > /dev/null 2>&1 &"

sleep 3

sshpass -p $pass ssh -f root@$server0 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s1 s2 s3 s6 s7 s8; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


sshpass -p $pass ssh -f root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s4 s5 s9 s10 s11; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


echo "==== STEP3: DONE ===="
