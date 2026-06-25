#!/bin/bash

# 设置网络延迟的完整脚本
# 用法: ./setup_network_delay.sh [interface] [delay_ms] [jitter_ms] [loss_percent] [bandwidth]
# 例如: ./setup_network_delay.sh eth0 25 10 0.01 1gbit

INTERFACE=${1:-eth0}
DELAY=${2:-25}
JITTER=${3:-10}
LOSS=${4:-0.01}
BANDWIDTH=${5:-1gbit}

# 检查是否为 root
if [[ $EUID -ne 0 ]]; then
   echo "此脚本必须以 root 身份运行"
   exit 1
fi

# 检查接口是否存在
if ! ip link show "$INTERFACE" > /dev/null 2>&1; then
    echo "错误: 接口 $INTERFACE 不存在"
    echo "可用接口:"
    ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//'
    exit 1
fi

echo "========== 网络延迟配置 =========="
echo "接口: $INTERFACE"
echo "延迟: ${DELAY}ms"
echo "抖动: ${JITTER}ms"
echo "丢包率: ${LOSS}%"
echo "带宽: $BANDWIDTH"
echo "===================================="

# 清除旧配置
echo "清除旧的 qdisc 配置..."
tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
sleep 1

# 方案1: 使用 fq_codel + netem (推荐，更稳定)
echo "配置网络延迟..."

# 添加根 qdisc (fq_codel - Fair Queuing with Controlled Delay)
tc qdisc add dev "$INTERFACE" root handle 1: fq_codel

# 添加 netem qdisc 用于延迟、抖动和丢包
tc qdisc add dev "$INTERFACE" parent 1: handle 10: netem \
    delay "${DELAY}ms" "${JITTER}ms" \
    loss "${LOSS}%"

echo "✓ 网络延迟配置完成"

# 显示配置
echo ""
echo "当前 qdisc 配置:"
tc qdisc show dev "$INTERFACE"
echo ""
echo "当前 class 配置:"
tc class show dev "$INTERFACE"
echo ""

# 验证
echo "验证配置 (ping 本地网关):"
GATEWAY=$(ip route | grep default | awk '{print $3}')
if [ ! -z "$GATEWAY" ]; then
    ping -c 3 "$GATEWAY" 2>/dev/null | tail -1
else
    echo "无法获取网关地址"
fi

echo ""
echo "清除配置命令:"
echo "  sudo tc qdisc del dev $INTERFACE root"
echo ""
echo "查看配置命令:"
echo "  tc qdisc show dev $INTERFACE"
echo "  tc -s qdisc show dev $INTERFACE"
