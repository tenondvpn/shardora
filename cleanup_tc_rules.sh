#!/bin/bash
# 清理所有 TC 网络模拟规则
# 用于恢复网络到正常状态

echo "清理 TC 网络模拟规则..."

# 获取主网络接口
INTERFACE=$(ip route | grep default | awk '{print $5}' | head -1)

if [ -z "$INTERFACE" ]; then
    echo "错误: 无法获取网络接口"
    exit 1
fi

echo "网络接口: $INTERFACE"

# 清理 TC 规则
echo "清理 qdisc..."
sudo tc qdisc del dev $INTERFACE root 2>/dev/null || true

echo "✓ TC 规则已清理"
echo "网络已恢复到正常状态"
