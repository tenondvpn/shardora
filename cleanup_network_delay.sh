#!/bin/bash

# 清除所有网络延迟配置

# 检查是否为 root
if [[ $EUID -ne 0 ]]; then
   echo "此脚本必须以 root 身份运行"
   exit 1
fi

echo "清除网络延迟配置..."

# 获取所有网络接口
INTERFACES=$(ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//')

for INTERFACE in $INTERFACES; do
    echo "清除接口: $INTERFACE"
    tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
    tc qdisc del dev "$INTERFACE" ingress 2>/dev/null || true
done

# 清除 ifb 设备
echo "清除虚拟设备..."
for i in 0 1 2 3 4 5; do
    tc qdisc del dev "ifb$i" root 2>/dev/null || true
    ip link set dev "ifb$i" down 2>/dev/null || true
done

echo "✓ 清除完成"

# 验证
echo ""
echo "验证清除结果:"
for INTERFACE in $INTERFACES; do
    if ip link show "$INTERFACE" | grep -q "UP"; then
        echo "$INTERFACE: $(tc qdisc show dev $INTERFACE | head -1)"
    fi
done
