#!/bin/bash
# 在远程服务器上配置 root 密码登录
# 用法: ssh ubuntu@host "sudo bash < setup_root_ssh.sh"
# 或:   ssh ubuntu@host "sudo bash -s" < setup_root_ssh.sh

PASS="${1:-Xf4aGbTaf&}"

# 设置 root 密码（用 printf 避免 & 被 shell 解释）
printf 'root:%s\n' "$PASS" | chpasswd

# 配置 sshd
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config

# 处理 sshd_config.d 中的覆盖
for f in /etc/ssh/sshd_config.d/*.conf; do
    [ -f "$f" ] && sed -i 's/^PasswordAuthentication no/PasswordAuthentication yes/' "$f"
done

# 重启 SSH
systemctl restart ssh 2>/dev/null || systemctl restart sshd 2>/dev/null

# 内核优化
grep -q "net.core.default_qdisc=fq" /etc/sysctl.conf || cat >> /etc/sysctl.conf <<'EOT'
net.core.default_qdisc=fq
net.ipv4.tcp_congestion_control=bbr
fs.file-max=1000000
net.ipv4.ip_local_port_range=1024 65535
net.core.rmem_max=134217728
net.core.wmem_max=134217728
net.ipv4.tcp_rmem=4096 87380 134217728
net.ipv4.tcp_wmem=4096 65536 134217728
net.core.somaxconn=10000
net.ipv4.tcp_tw_reuse=1
net.ipv4.tcp_fin_timeout=30
EOT

grep -q "soft nofile 1000000" /etc/security/limits.conf || cat >> /etc/security/limits.conf <<'EOT'
* soft nofile 1000000
* hard nofile 1000000
root soft nofile 1000000
root hard nofile 1000000
EOT

sysctl -p 2>/dev/null || true

echo "SSH_SETUP_OK"
