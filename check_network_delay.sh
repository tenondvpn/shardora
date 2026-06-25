#!/bin/bash

# 检查网络延迟配置是否生效

INTERFACE=${1:-eth0}

echo "========== 网络延迟诊断 =========="
echo "检查接口: $INTERFACE"
echo ""

# 检查接口是否存在
if ! ip link show "$INTERFACE" > /dev/null 2>&1; then
    echo "错误: 接口 $INTERFACE 不存在"
    echo "可用接口:"
    ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//'
    exit 1
fi

# 显示接口状态
echo "1. 接口状态:"
ip link show "$INTERFACE"
echo ""

# 显示 qdisc 配置
echo "2. Qdisc 配置:"
tc qdisc show dev "$INTERFACE"
echo ""

# 显示详细的 qdisc 统计
echo "3. Qdisc 统计信息:"
tc -s qdisc show dev "$INTERFACE"
echo ""

# 显示 class 配置
echo "4. Class 配置:"
tc class show dev "$INTERFACE"
echo ""

# 显示 filter 配置
echo "5. Filter 配置:"
tc filter show dev "$INTERFACE"
echo ""

# 获取网关
GATEWAY=$(ip route | grep default | awk '{print $3}')
if [ ! -z "$GATEWAY" ]; then
    echo "6. Ping 网关测试 ($GATEWAY):"
    ping -c 5 "$GATEWAY" 2>/dev/null | tail -3
else
    echo "6. 无法获取网关地址"
fi

echo ""
echo "========== 诊断完成 =========="
echo ""
echo "如果延迟没有生效，可能的原因:"
echo "1. 网络接口名称不正确 (检查 ip link show)"
echo "2. tc 命令未安装 (apt-get install iproute2)"
echo "3. 内核不支持 netem (需要 CONFIG_NET_SCH_NETEM=y)"
echo "4. 延迟配置在错误的接口上"
echo ""
echo "清除配置:"
echo "  sudo tc qdisc del dev $INTERFACE root"
