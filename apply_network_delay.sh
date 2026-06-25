#!/bin/bash

# 应用网络延迟到所有活跃接口
# 用法: ./apply_network_delay.sh [delay_ms] [jitter_ms] [loss_percent]

DELAY=${1:-25}
JITTER=${2:-10}
LOSS=${3:-0.01}

# 检查是否为 root
if [[ $EUID -ne 0 ]]; then
   echo "此脚本必须以 root 身份运行"
   exit 1
fi

echo "========== 应用网络延迟 =========="
echo "延迟: ${DELAY}ms"
echo "抖动: ${JITTER}ms"
echo "丢包率: ${LOSS}%"
echo "===================================="

# 获取所有活跃的网络接口（排除 lo）
INTERFACES=$(ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//' | grep -v "^lo$")

if [ -z "$INTERFACES" ]; then
    echo "错误: 未找到网络接口"
    exit 1
fi

echo "检测到的网络接口:"
echo "$INTERFACES"
echo ""

# 对每个接口应用延迟配置
for INTERFACE in $INTERFACES; do
    echo "配置接口: $INTERFACE"
    
    # 检查接口是否 UP
    if ! ip link show "$INTERFACE" | grep -q "UP"; then
        echo "  跳过 (接口未启用)"
        continue
    fi
    
    # 清除旧配置
    tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
    sleep 0.5
    
    # 应用新配置
    tc qdisc add dev "$INTERFACE" root handle 1: fq_codel
    tc qdisc add dev "$INTERFACE" parent 1: handle 10: netem \
        delay "${DELAY}ms" "${JITTER}ms" \
        loss "${LOSS}%"
    
    echo "  ✓ 配置完成"
done

echo ""
echo "========== 配置完成 =========="
echo ""
echo "验证配置:"
for INTERFACE in $INTERFACES; do
    if ip link show "$INTERFACE" | grep -q "UP"; then
        echo ""
        echo "接口 $INTERFACE:"
        tc qdisc show dev "$INTERFACE"
    fi
done

echo ""
echo "清除所有延迟配置:"
echo "  for iface in $INTERFACES; do sudo tc qdisc del dev \$iface root 2>/dev/null || true; done"
