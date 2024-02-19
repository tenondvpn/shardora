#!/bin/bash
# 修改配置文件
ssh root@10.101.20.35 <<'EOF'
cd /root && sh -x fetch.sh 127.0.0.1 10.101.20.35 r1 r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 node
EOF

ssh root@10.101.20.33 <<'EOF'
cd /root && sh -x fetch.sh 10.101.20.35 10.101.20.33 r1 r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 node
EOF



# 启动进程
# ssh root@10.101.20.35 <<'EOF'
# cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 &
# EOF

# sleep 3

# # 在服务器 10.1.1.1 上执行的命令
# ssh root@10.101.20.35 <<'EOF'
# cd /root/zjnodes/r2/ && nohup ./zjchain -f 0 -g 0 r2 &

# cd /root/zjnodes/s1/ && nohup ./zjchain -f 0 -g 0 s1 &
# cd /root/zjnodes/s2/ && nohup ./zjchain -f 0 -g 0 s2 &

# cd /root/zjnodes/s6/ && nohup ./zjchain -f 0 -g 0 s6 &
# cd /root/zjnodes/s7/ && nohup ./zjchain -f 0 -g 0 s7 &
# cd /root/zjnodes/s8/ && nohup ./zjchain -f 0 -g 0 s8 &
# EOF

# # 在服务器 10.1.1.2 上执行的命令
# ssh root@10.101.20.33 <<'EOF'
# cd /root/zjnodes/r3/ && nohup ./zjchain -f 0 -g 0 r3 &

# cd /root/zjnodes/s3/ && nohup ./zjchain -f 0 -g 0 s3 &
# cd /root/zjnodes/s4/ && nohup ./zjchain -f 0 -g 0 s4 &
# cd /root/zjnodes/s5/ && nohup ./zjchain -f 0 -g 0 s5 &

# cd /root/zjnodes/s9/ && nohup ./zjchain -f 0 -g 0 s9 &
# cd /root/zjnodes/s10/ && nohup ./zjchain -f 0 -g 0 s10 &
# cd /root/zjnodes/s11/ && nohup ./zjchain -f 0 -g 0 s11 &
# EOF
