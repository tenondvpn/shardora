#!/bin/bash

# 网络延迟诊断脚本
# 用法: sudo bash diagnose_network.sh [interface]

INTERFACE=${1:-eth0}

echo "========== 网络延迟诊断 =========="
echo ""

# 检查是否为 root
if [[ $EUID -ne 0 ]]; then
   echo "❌ 此脚本必须以 root 身份运行"
   exit 1
fi

# 1. 检查网络接口
echo "1️⃣  检查网络接口..."
echo "所有可用接口:"
ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//'
echo ""

if ! ip link show "$INTERFACE" > /dev/null 2>&1; then
    echo "❌ 接口 $INTERFACE 不存在"
    exit 1
fi

echo "✓ 接口 $INTERFACE 存在"
ip link show "$INTERFACE" | head -1
echo ""

# 2. 检查 tc 命令
echo "2️⃣  检查 tc 命令..."
if ! command -v tc &> /dev/null; then
    echo "❌ tc 命令未安装"
    echo "安装命令: apt-get install -y iproute2"
    exit 1
fi
echo "✓ tc 命令已安装"
tc --version
echo ""

# 3. 检查内核支持
echo "3️⃣  检查内核支持..."
if grep -q "CONFIG_NET_SCH_NETEM=y" /boot/config-$(uname -r) 2>/dev/null; then
    echo "✓ 内核支持 netem (CONFIG_NET_SCH_NETEM=y)"
else
    echo "⚠️  无法确认内核配置，尝试加载 sch_netem 模块..."
    modprobe sch_netem 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ sch_netem 模块加载成功"
    else
        echo "❌ 无法加载 sch_netem 模块，内核可能不支持"
    fi
fi
echo ""

# 4. 检查当前 qdisc 配置
echo "4️⃣  检查当前 qdisc 配置..."
echo "接口 $INTERFACE 的 qdisc:"
tc qdisc show dev "$INTERFACE"
if [ $? -ne 0 ]; then
    echo "❌ 无法查询 qdisc 配置"
else
    echo "✓ qdisc 配置查询成功"
fi
echo ""

# 5. 检查 ifb 模块
echo "5️⃣  检查 ifb 模块..."
if lsmod | grep -q "ifb"; then
    echo "✓ ifb 模块已加载"
else
    echo "⚠️  ifb 模块未加载，尝试加载..."
    modprobe ifb 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ ifb 模块加载成功"
    else
        echo "❌ 无法加载 ifb 模块"
    fi
fi
echo ""

# 6. 测试延迟配置
echo "6️⃣  测试延迟配置..."
echo "清除旧配置..."
tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
tc qdisc del dev "$INTERFACE" ingress 2>/dev/null || true
sleep 1

echo "应用测试配置 (50ms 延迟)..."
tc qdisc add dev "$INTERFACE" root handle 1: fq_codel 2>/dev/null
if [ $? -ne 0 ]; then
    echo "❌ 无法添加 fq_codel qdisc"
    exit 1
fi

tc qdisc add dev "$INTERFACE" parent 1: handle 10: netem delay 50ms 10ms loss 0.01% 2>/dev/null
if [ $? -ne 0 ]; then
    echo "❌ 无法添加 netem qdisc"
    tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
    exit 1
fi

echo "✓ 测试配置应用成功"
echo ""

# 7. 验证配置
echo "7️⃣  验证配置..."
tc qdisc show dev "$INTERFACE"
echo ""

# 8. Ping 测试
echo "8️⃣  Ping 测试..."
GATEWAY=$(ip route | grep default | awk '{print $3}')
if [ ! -z "$GATEWAY" ]; then
    echo "Ping 网关 $GATEWAY (应该看到约 50ms 的延迟):"
    ping -c 5 "$GATEWAY" 2>/dev/null | tail -3
else
    echo "⚠️  无法获取网关地址"
fi
echo ""

# 9. 清除测试配置
echo "9️⃣  清除测试配置..."
tc qdisc del dev "$INTERFACE" root 2>/dev/null || true
tc qdisc del dev "$INTERFACE" ingress 2>/dev/null || true
echo "✓ 测试配置已清除"
echo ""

echo "========== 诊断完成 =========="
echo ""
echo "如果延迟仍未生效，请检查:"
echo "1. 网络接口是否正确 (ip link show)"
echo "2. 是否有防火墙规则阻止 (iptables -L)"
echo "3. 是否在虚拟机/容器中运行 (可能不支持 tc)"
echo "4. 是否需要在两端都配置延迟"
echo ""
echo "应用延迟配置:"
echo "  sudo bash setup_bidirectional_delay.sh $INTERFACE 25 10 0.01"
echo ""
echo "清除所有延迟配置:"
echo "  sudo bash cleanup_network_delay.sh"
