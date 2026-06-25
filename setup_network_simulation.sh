#!/bin/bash

# Ubuntu 一键设置局域网模拟公网
# 参数: 1G带宽, 50ms延迟, 10ms抖动, 1/10000丢包率

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查是否为 root
if [[ $EUID -ne 0 ]]; then
   echo -e "${RED}此脚本必须以 root 身份运行${NC}"
   exit 1
fi

# 获取网络接口
if [ -z "$1" ]; then
    echo -e "${YELLOW}请指定网络接口，例如: eth0, ens0, wlan0${NC}"
    echo "可用接口:"
    ip link show | grep "^[0-9]" | awk '{print $2}' | sed 's/:$//'
    exit 1
fi

INTERFACE=$1
BANDWIDTH="1gbit"
DELAY="50ms"
JITTER="10ms"
LOSS="0.01%"  # 1/10000 = 0.01%

echo -e "${GREEN}========== 网络模拟配置 ==========${NC}"
echo "接口: $INTERFACE"
echo "带宽: $BANDWIDTH"
echo "延迟: $DELAY"
echo "抖动: $JITTER"
echo "丢包率: $LOSS"
echo -e "${GREEN}===================================${NC}"

# 检查接口是否存在
if ! ip link show "$INTERFACE" > /dev/null 2>&1; then
    echo -e "${RED}错误: 接口 $INTERFACE 不存在${NC}"
    exit 1
fi

# 安装必要工具
echo -e "${YELLOW}检查并安装必要工具...${NC}"
if ! command -v tc &> /dev/null; then
    apt-get update
    apt-get install -y iproute2
fi

# 清除旧的 qdisc 配置
echo -e "${YELLOW}清除旧的 qdisc 配置...${NC}"
tc qdisc del dev "$INTERFACE" root 2>/dev/null || true

# 添加根 qdisc (HTB - Hierarchical Token Bucket)
echo -e "${YELLOW}配置带宽限制...${NC}"
tc qdisc add dev "$INTERFACE" root handle 1: htb default 1

# 创建类 (class) 限制带宽
tc class add dev "$INTERFACE" parent 1: classid 1:1 htb rate "$BANDWIDTH"

# 添加 netem qdisc 用于延迟、抖动和丢包
echo -e "${YELLOW}配置延迟、抖动和丢包...${NC}"
tc qdisc add dev "$INTERFACE" parent 1:1 handle 10: netem \
    delay "$DELAY" "$JITTER" \
    loss "$LOSS"

echo -e "${GREEN}✓ 网络模拟配置完成${NC}"

# 显示配置
echo -e "${GREEN}========== 当前配置 ==========${NC}"
tc qdisc show dev "$INTERFACE"
echo ""
tc class show dev "$INTERFACE"
echo ""
tc qdisc show dev "$INTERFACE" parent 1:1
echo -e "${GREEN}==============================${NC}"

# 验证配置
echo -e "${YELLOW}验证配置...${NC}"
echo "使用以下命令测试:"
echo "  ping <目标IP>  # 应该看到约 50ms 的延迟"
echo "  iperf3 -c <目标IP>  # 应该看到约 1Gbps 的带宽"
echo ""
echo -e "${YELLOW}清除配置命令:${NC}"
echo "  sudo tc qdisc del dev $INTERFACE root"
echo ""
echo -e "${YELLOW}查看配置命令:${NC}"
echo "  tc qdisc show dev $INTERFACE"
echo "  tc class show dev $INTERFACE"
