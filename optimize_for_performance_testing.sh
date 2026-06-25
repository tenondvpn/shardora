#!/bin/bash

# 性能压测优化脚本
# 用法: ./optimize_for_performance_testing.sh

set -e

echo "=========================================="
echo "性能压测优化脚本"
echo "=========================================="

# 1. 禁用网络延迟
echo ""
echo "[1/4] 禁用网络延迟注入..."
export SHARDORA_NETWORK_ENABLED=0
export SHARDORA_NETWORK_DELAY_MS=0
export SHARDORA_NETWORK_JITTER_MS=0
export SHARDORA_NETWORK_LOSS_RATE=0
echo "✓ 网络延迟已禁用"

# 2. 优化系统参数
echo ""
echo "[2/4] 优化系统TCP参数..."

# 检查是否有sudo权限
if [ "$EUID" -eq 0 ]; then
    # 增加文件描述符限制
    ulimit -n 65536
    echo "✓ 文件描述符限制: 65536"
    
    # 优化TCP缓冲区
    sysctl -w net.core.rmem_max=134217728 > /dev/null 2>&1
    sysctl -w net.core.wmem_max=134217728 > /dev/null 2>&1
    echo "✓ TCP缓冲区已优化 (128MB)"
    
    # 优化TCP参数
    sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728" > /dev/null 2>&1
    sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728" > /dev/null 2>&1
    echo "✓ TCP读写缓冲已优化"
    
    # 优化TCP连接
    sysctl -w net.ipv4.tcp_tw_reuse=1 > /dev/null 2>&1
    sysctl -w net.ipv4.tcp_fin_timeout=30 > /dev/null 2>&1
    echo "✓ TCP连接复用已启用"
else
    echo "⚠ 需要sudo权限来优化系统参数"
    echo "  请运行: sudo ./optimize_for_performance_testing.sh"
fi

# 3. 显示当前配置
echo ""
echo "[3/4] 当前配置:"
echo "  SHARDORA_NETWORK_ENABLED=$SHARDORA_NETWORK_ENABLED"
echo "  SHARDORA_NETWORK_DELAY_MS=$SHARDORA_NETWORK_DELAY_MS"
echo "  SHARDORA_NETWORK_JITTER_MS=$SHARDORA_NETWORK_JITTER_MS"
echo "  SHARDORA_NETWORK_LOSS_RATE=$SHARDORA_NETWORK_LOSS_RATE"

# 4. 显示下一步
echo ""
echo "[4/4] 下一步:"
echo "  1. 重新编译项目:"
echo "     cd /root/shardora/cbuild_Release"
echo "     make -j4"
echo ""
echo "  2. 运行部署脚本:"
echo "     ./temp_cmd.sh"
echo ""
echo "  3. 监控日志:"
echo "     tail -f logfile.txt | grep -E 'oversized|on_read error|connection refused'"
echo ""

echo "=========================================="
echo "✓ 优化完成！"
echo "=========================================="
