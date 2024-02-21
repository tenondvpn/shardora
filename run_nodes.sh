
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=10.101.20.35
server1=10.101.20.32
server2=10.101.20.33
target=$1

echo "[$server0]"
sshpass -p !@#$%^ ssh root@$server0 <<EOF
cd /root/xufei/zjchain && sh deploy_genesis.sh $target ${server0}
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} $pass r1 r2 s3_1 s3_2 s3_3 s4_1 s4_2 s4_3 s4_4 s5_1 s5_2
EOF


echo "[$server1]"
sshpass -p !@#$%^ ssh root@$server1 <<EOF
sshpass -p !@#$%^ scp root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server1} !@#$%^ s3_6 s4_7 s5_3
EOF


echo "[$server2]"
sshpass -p !@#$%^ ssh root@$server2 <<EOF
sshpass -p !@#$%^ scp root@"${server0}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${server0} ${server2} !@#$%^ r3 s3_4 s3_5 s4_5 s4_6
EOF


echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="


echo "[$server0]"
sshpass -p !@#$%^ ssh root@$server0 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server1]"
sshpass -p !@#$%^ ssh root@$server1 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "[$server2]"
sshpass -p !@#$%^ ssh root@$server2 <<"EOF"
ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
EOF

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
sshpass -p !@#$%^ ssh -f root@$server0 "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 > /dev/null 2>&1 &"

sleep 3

sshpass -p !@#$%^ ssh -f root@$server0 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r2 s3_1 s3_2 s3_3 s4_1 s4_2 s4_3 s4_4 s5_1 s5_2; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


sshpass -p !@#$%^ ssh -f root@$server1 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in s3_6 s4_7 s5_3; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


sshpass -p !@#$%^ ssh -f root@$server2 bash -c "'\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \
for node in r3 s3_4 s3_5 s4_5 s4_6; do \
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node > /dev/null 2>&1 &\
done \
'"


echo "==== STEP3: DONE ===="
