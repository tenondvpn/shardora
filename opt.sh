#!/bin/bash

# 检查是否为 root 用户
if [ "$EUID" -ne 0 ]; then
  echo "请使用 root 权限运行此脚本 (sudo ./opt.sh)"
  exit
fi

echo "======================================================="
echo "   开始执行 Linux 高并发网络与系统参数优化脚本"
echo "   针对问题：getpeername error, 连接数受限, 高丢包环境"
echo "======================================================="

# 1. 备份配置文件
echo "[+] 正在备份配置文件..."
cp /etc/sysctl.conf /etc/sysctl.conf.bak.$(date +%F_%T)
cp /etc/security/limits.conf /etc/security/limits.conf.bak.$(date +%F_%T)
echo "    备份完成。"

# 2. 优化 Sysctl (内核网络栈)
echo "[+] 正在写入内核优化参数到 /etc/sysctl.conf..."

cat >> /etc/sysctl.conf <<EOF

# --- Added by Optimization Script $(date) ---
# 允许重用 TIME_WAIT socket，解决大量连接断开后的端口耗尽问题
net.ipv4.tcp_tw_reuse = 1

# 缩短 Socket 进入 FIN-WAIT-2 状态的时间
net.ipv4.tcp_fin_timeout = 15

# 扩大端口范围
net.ipv4.ip_local_port_range = 1024 65535

# 增加连接队列长度 (防止并发握手时丢包)
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535

# 增加 TCP 读写缓冲区 (应对高吞吐)
net.core.rmem_default = 262144
net.core.rmem_max = 16777216
net.core.wmem_default = 262144
net.core.wmem_max = 16777216
net.ipv4.tcp_rmem = 4096 87380 16777216
net.ipv4.tcp_wmem = 4096 65536 16777216

# 开启 TCP Fast Open (可选，降低延迟)
net.ipv4.tcp_fastopen = 3

# 针对高丢包环境的拥塞控制 (如果有 BBR 模块会自动生效，没有则忽略)
# net.core.default_qdisc = fq
# net.ipv4.tcp_congestion_control = bbr

# 系统级最大文件打开数
fs.file-max = 1000000
# --- End Optimization ---
EOF

# 应用 Sysctl 更改
echo "[+] 应用内核参数..."
sysctl -p
if [ $? -eq 0 ]; then
    echo "    内核参数应用成功。"
else
    echo "    警告：部分参数应用失败，请检查错误信息。"
fi

# 3. 优化 Ulimit (文件描述符限制)
echo "[+] 正在修改文件描述符限制 /etc/security/limits.conf..."

cat >> /etc/security/limits.conf <<EOF

# --- Added by Optimization Script $(date) ---
* soft nofile 1000000
* hard nofile 1000000
root soft nofile 1000000
root hard nofile 1000000
# --- End Optimization ---
EOF

# 4. 确保 PAM 加载 limits 配置
echo "[+] 检查 PAM 配置..."
if grep -q "pam_limits.so" /etc/pam.d/common-session; then
    echo "    pam_limits.so 已存在，跳过。"
else
    echo "session required pam_limits.so" >> /etc/pam.d/common-session
    echo "    已添加 pam_limits.so 到 common-session。"
fi

echo "======================================================="
echo "   优化完成！"
echo "======================================================="
echo "1. 请重新登录终端，或者重启相关服务以使 ulimit 生效。"
echo "2. 验证命令："
echo "   - 查看连接队列: sysctl net.core.somaxconn"
echo "   - 查看文件限制: ulimit -n (应显示 1000000)"
echo ""
echo "注意：该脚本优化了系统承载能力，但 'getpeername error' "
echo "本质是 C++ 代码在连接断开后依然尝试读取信息。"
echo "如果日志依然刷屏，请修改代码忽略 ENOTCONN 错误。"
echo "======================================================="
