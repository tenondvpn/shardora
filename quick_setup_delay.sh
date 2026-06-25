#!/bin/bash

# 快速设置网络延迟 - 自动检测接口
# 用法: sudo bash quick_setup_delay.sh [delay_ms] [jitter_ms] [loss_percent]

DELAY=${1:-25}
JITTER=${2:-10}
LOSS=${3:-0.01}

# 检查是否为 root
if [[ $EUID -ne 0 ]]; then
   echo "❌ 此脚本必须以 root 身份运行"
   exit 1
fi

echo "========== 快速设置网络延迟 =========="
echo "延迟: ${DELAY}ms (单向)"
echo "抖动: ${JITTER}ms"
echo "丢包率: ${LOSS}%"
echo "======================================="
echo ""

# 1. 检查并安装依赖
echo "1️⃣  检查依赖..."
if ! command -v tc &> /dev/null; then
    echo "⚠️  tc 命令未安装，正在安装..."
    apt-get update > /dev/null 2>&1
    apt-get install -y iproute2 > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "❌ 安装失败"
        exit 1
    fi
    echo "✓ iproute2 安装成功"
else
    echo "✓ tc 命令已安装"
fi
echo ""

# 2. 加载必要的内核模块
echo "2️⃣  加载内核模块..."
modprobe sch_netem 2>/dev/null
modprobe ifb 2>/dev/null
echo "✓ 内核模块加载完成"
echo ""

# 3. 自动检测主网络接口
echo "3️⃣  检测网络接口..."
INTERFACE=$(ip route | grep default | awk '{print $5}' | head -1)
if [ -z "$INTERFACE" ]; then
    echo "❌ 无法自动检测网络接口"
    echo "可用接口:"
    ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//'
    exit 1
fi
echo "✓ 检测到主接口: $INTERFACE"
echo ""

# 4. 清除旧配置
echo "4️⃣  清除旧配置..."
tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
tc qdisc del dev "$INTERFACE" ingress 2>/dev/null || true
tc qdisc del dev ifb0 root 2>/dev/null || true
sleep 1
echo "✓ 旧配置已清除"
echo ""

# 5. 应用出站流量延迟
echo "5️⃣  配置出站流量延迟..."
tc qdisc add dev "$INTERFACE" root handle 1: fq_codel
tc qdisc add dev "$INTERFACE" parent 1: handle 10: netem \
    delay "${DELAY}ms" "${JITTER}ms" \
    loss "${LOSS}%"
if [ $? -ne 0 ]; then
    echo "❌ 配置失败"
    exit 1
fi
echo "✓ 出站流量延迟配置完成"
echo ""

# 6. 应用入站流量延迟
echo "6️⃣  配置入站流量延迟..."
ip link set dev ifb0 down 2>/dev/null || true
ip link set dev ifb0 up 2>/dev/null || true
tc qdisc add dev "$INTERFACE" ingress handle ffff:
tc filter add dev "$INTERFACE" parent ffff: protocol ip u32 match u32 0 0 flowid 1:1 action mirred egress redirect dev ifb0
tc qdisc add dev ifb0 root handle 1: fq_codel
tc qdisc add dev ifb0 parent 1: handle 10: netem \
    delay "${DELAY}ms" "${JITTER}ms" \
    loss "${LOSS}%"
if [ $? -ne 0 ]; then
    echo "⚠️  入站流量配置可能失败，但出站流量已配置"
else
    echo "✓ 入站流量延迟配置完成"
fi
echo ""

# 7. 显示配置
echo "7️⃣  显示配置..."
echo "出站流量 ($INTERFACE):"
tc qdisc show dev "$INTERFACE"
echo ""
echo "入站流量 (ifb0):"
tc qdisc show dev ifb0
echo ""

# 8. 验证
echo "8️⃣  验证配置..."
GATEWAY=$(ip route | grep default | awk '{print $3}')
if [ ! -z "$GATEWAY" ]; then
    echo "Ping 网关 $GATEWAY (应该看到约 $((DELAY*2))ms 的延迟):"
    ping -c 3 "$GATEWAY" 2>/dev/null | tail -1
else
    echo "⚠️  无法获取网关地址"
fi
echo ""

echo "========== 配置完成 =========="
echo ""
echo "清除配置:"
echo "  sudo bash cleanup_network_delay.sh"
echo ""
echo "诊断:"
echo "  sudo bash diagnose_network.sh $INTERFACE"
