#!/bin/bash

# 设置双向网络延迟 (对所有流量生效)
# 用法: ./setup_bidirectional_delay.sh [interface] [delay_ms] [jitter_ms] [loss_percent]
# 例如: ./setup_bidirectional_delay.sh eth0 25 10 0.01

INTERFACE=${1:-eth0}
DELAY=${2:-25}
JITTER=${3:-10}
LOSS=${4:-0.01}

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

echo "========== 设置双向网络延迟 =========="
echo "接口: $INTERFACE"
echo "延迟: ${DELAY}ms (单向)"
echo "抖动: ${JITTER}ms"
echo "丢包率: ${LOSS}%"
echo "======================================="

# 清除旧配置
echo "清除旧配置..."
tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
tc qdisc del dev "$INTERFACE" ingress 2>/dev/null || true
sleep 1

# 方案: 使用 ifb (Intermediate Functional Block) 处理入站流量
# 1. 创建虚拟 ifb 设备
IFB_DEV="ifb0"
modprobe ifb 2>/dev/null || true
ip link set dev "$IFB_DEV" down 2>/dev/null || true
ip link set dev "$IFB_DEV" up 2>/dev/null || true

echo "配置出站流量延迟..."
# 2. 配置出站流量 (egress)
tc qdisc add dev "$INTERFACE" root handle 1: fq_codel
tc qdisc add dev "$INTERFACE" parent 1: handle 10: netem \
    delay "${DELAY}ms" "${JITTER}ms" \
    loss "${LOSS}%"

echo "配置入站流量延迟..."
# 3. 配置入站流量 (ingress) - 通过 ifb 重定向
tc qdisc add dev "$INTERFACE" ingress handle ffff:
tc filter add dev "$INTERFACE" parent ffff: protocol ip u32 match u32 0 0 flowid 1:1 action mirred egress redirect dev "$IFB_DEV"

# 4. 在 ifb 设备上配置延迟
tc qdisc add dev "$IFB_DEV" root handle 1: fq_codel
tc qdisc add dev "$IFB_DEV" parent 1: handle 10: netem \
    delay "${DELAY}ms" "${JITTER}ms" \
    loss "${LOSS}%"

echo "✓ 双向网络延迟配置完成"

# 显示配置
echo ""
echo "出站流量 ($INTERFACE) 配置:"
tc qdisc show dev "$INTERFACE"
echo ""
echo "入站流量 ($IFB_DEV) 配置:"
tc qdisc show dev "$IFB_DEV"
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
echo "  sudo tc qdisc del dev $INTERFACE ingress"
echo "  sudo tc qdisc del dev $IFB_DEV root"
echo ""
echo "或运行:"
echo "  sudo bash cleanup_network_delay.sh"
