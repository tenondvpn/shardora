#!/bin/bash
# 应用层网络延迟注入脚本
# 用于启动 Shardora 节点时注入网络延迟和抖动
# 避免 TCP 层报文被破坏的问题

# 网络模拟参数
NETWORK_DELAY_MS=${SHARDORA_NETWORK_DELAY_MS:-25}      # 单向延迟 25ms (往返 50ms)
NETWORK_JITTER_MS=${SHARDORA_NETWORK_JITTER_MS:-10}    # 抖动 10ms
NETWORK_LOSS_RATE=${SHARDORA_NETWORK_LOSS_RATE:-0.0001} # 丢包率 0.01% (1/10000)
NETWORK_ENABLED=${SHARDORA_NETWORK_ENABLED:-0}         # 是否启用网络模拟

echo "=========================================="
echo "应用层网络延迟注入配置"
echo "=========================================="
echo "延迟 (单向):     $NETWORK_DELAY_MS ms"
echo "抖动:           $NETWORK_JITTER_MS ms"
echo "丢包率:         $NETWORK_LOSS_RATE ($(echo "scale=4; $NETWORK_LOSS_RATE * 100" | bc)%)"
echo "启用状态:       $([ "$NETWORK_ENABLED" = "1" ] && echo "已启用" || echo "已禁用")"
echo "=========================================="

# 导出环境变量供应用使用
export SHARDORA_NETWORK_ENABLED=$NETWORK_ENABLED
export SHARDORA_NETWORK_DELAY_MS=$NETWORK_DELAY_MS
export SHARDORA_NETWORK_JITTER_MS=$NETWORK_JITTER_MS
export SHARDORA_NETWORK_LOSS_RATE=$NETWORK_LOSS_RATE

echo ""
echo "环境变量已设置，可以启动 Shardora 节点"
echo ""
echo "使用示例:"
echo "  # 启用网络模拟"
echo "  export SHARDORA_NETWORK_ENABLED=1"
echo "  export SHARDORA_NETWORK_DELAY_MS=25"
echo "  export SHARDORA_NETWORK_JITTER_MS=10"
echo "  export SHARDORA_NETWORK_LOSS_RATE=0.0001"
echo "  ./shardora"
echo ""
echo "  # 禁用网络模拟"
echo "  export SHARDORA_NETWORK_ENABLED=0"
echo "  ./shardora"
echo ""
