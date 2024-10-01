#!/bin/bash

server0=10.0.0.201
servers=(
	10.0.0.1
	10.0.0.2
	10.0.0.3
	10.0.0.4
	10.0.0.5
	10.0.0.6
	10.0.0.7
	10.0.0.8
	10.0.0.9
	10.0.0.10
	10.0.0.11
	10.0.0.12
	10.0.0.13
	10.0.0.14
	10.0.0.15
	10.0.0.16
	10.0.0.17
	10.0.0.18
	10.0.0.19
	10.0.0.20
	10.0.0.21
	10.0.0.22
	10.0.0.23
	10.0.0.24
	10.0.0.25
	10.0.0.26
	10.0.0.27
	10.0.0.28
	10.0.0.29
	10.0.0.30
	10.0.0.31
	10.0.0.32
	10.0.0.33
	10.0.0.34
	10.0.0.35
	10.0.0.36
	10.0.0.37
	10.0.0.38
	10.0.0.39
	10.0.0.40
	10.0.0.41
	10.0.0.42
	10.0.0.43
	10.0.0.44
	10.0.0.45
	10.0.0.46
	10.0.0.47
	10.0.0.48
	10.0.0.49
	10.0.0.50
	10.0.0.51
	10.0.0.52
	10.0.0.53
	10.0.0.54
	10.0.0.55
	10.0.0.56
	10.0.0.57
	10.0.0.58
	10.0.0.59
	10.0.0.60
	10.0.0.61
	10.0.0.62
	10.0.0.63
	10.0.0.64
	10.0.0.65
	10.0.0.66
	10.0.0.67
	10.0.0.68
	10.0.0.69
	10.0.0.70
	10.0.0.71
	10.0.0.72
	10.0.0.73
	10.0.0.74
	10.0.0.75
	10.0.0.76
	10.0.0.77
	10.0.0.78
	10.0.0.79
	10.0.0.80
	10.0.0.81
	10.0.0.82
	10.0.0.83
	10.0.0.84
	10.0.0.85
	10.0.0.86
	10.0.0.87
	10.0.0.88
	10.0.0.89
	10.0.0.90
	10.0.0.91
	10.0.0.92
	10.0.0.93
	10.0.0.94
	10.0.0.95
	10.0.0.96
	10.0.0.97
	10.0.0.98
	10.0.0.99
	10.0.0.100
	10.0.0.101
	10.0.0.102
	10.0.0.103
	10.0.0.104
	10.0.0.105
	10.0.0.106
	10.0.0.107
	10.0.0.108
	10.0.0.109
	10.0.0.110
	10.0.0.111
	10.0.0.112
	10.0.0.113
	10.0.0.114
	10.0.0.115
	10.0.0.116
	10.0.0.117
	10.0.0.118
	10.0.0.119
)

pass='Xf4aGbTaf!'

for server in "${servers[@]}"; do
(
echo "[$server] == start"
sshpass -p ${pass} ssh -o StrictHostKeyChecking=no root@$server <<EOF
chmod 777 /mnt
sudo yum group install -y "Development Tools"
sudo yum install -y glibc-devel glibc-headers
sudo yum install -y openssl-devel
sudo yum install -y sshpass

mkdir -p /root/xf;

sshpass -p '${pass}' scp -o StrictHostKeyChecking=no root@"${server0}":/root/gcc-8.3.0.tar.gz /root/
sshpass -p '${pass}' scp -o StrictHostKeyChecking=no root@"${server0}":/root/cmake-3.25.1-linux-x86_64.tar.gz /root/
cd /root && tar -zxvf gcc-8.3.0.tar.gz
mv gcc-8.3.0 /usr/local
cd /root && tar -zxvf cmake-3.25.1-linux-x86_64.tar.gz
mv cmake-3.25.1-linux-x86_64 /usr/local/cmake


echo 'export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/' >> ~/.bashrc
echo 'export CC=/usr/local/gcc-8.3.0/bin/gcc' >> ~/.bashrc
echo 'export PATH=/usr/local/gcc-8.3.0/bin:$PATH' >> ~/.bashrc
echo 'export PATH=/usr/local/cmake/bin:$PATH' >> ~/.bashrc

source ~/.bashrc

gcc --version
cmake --version

sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216
sudo sysctl -w net.ipv4.tcp_rmem='4096 87380 16777216'
sudo sysctl -w net.ipv4.tcp_wmem='4096 65536 16777216'
sudo sysctl -w net.ipv4.tcp_max_syn_backlog=4096
sudo sysctl -w net.core.somaxconn=4096
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"

echo 'net.core.rmem_max=16777216' | sudo tee -a /etc/sysctl.conf
echo 'net.core.wmem_max=16777216' | sudo tee -a /etc/sysctl.conf
echo 'net.ipv4.tcp_rmem=4096 87380 16777216' | sudo tee -a /etc/sysctl.conf
echo 'net.ipv4.tcp_wmem=4096 65536 16777216' | sudo tee -a /etc/sysctl.conf
echo 'net.ipv4.tcp_max_syn_backlog=4096' | sudo tee -a /etc/sysctl.conf
echo 'net.core.somaxconn=4096' | sudo tee -a /etc/sysctl.conf
echo 'net.ipv4.ip_local_port_range=1024 65535' | sudo tee -a /etc/sysctl.conf

sudo sysctl -p

ulimit -c unlimited
ulimit -n 65535

echo "[$server] == done"
EOF
) &
done

wait
