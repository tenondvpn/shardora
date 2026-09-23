#!/bin/bash
# ============================================================
# Chainbaas 前端重新部署脚本
#
# 功能:
#   1. 本地构建 Vue.js 前端 (npm run build)
#   2. 将 dist/、explorer_proxy.py、solc_server.py 打包上传
#   3. 在各目标节点安装 nginx 配置并重启服务
#
# 节点配置:
#   shard2: 192.168.26.203:22001
#   shard3: 192.168.26.213:23001
#   shard4: 192.168.26.211:24001
#   shard5: 192.168.26.204:25001
#   shard6: 192.168.26.215:26001
#
# 用法:
#   bash deploy_chainbaas.sh [选项]
#
# 选项:
#   -H IP[,IP,...]   指定部署目标主机（逗号分隔），默认: 192.168.26.203
#   -a               部署到所有节点 (5台)
#   -p PASSWORD      SSH 密码（默认: 1）
#   -P PORT          SSH 端口（默认: 22）
#   -s               仅跳过前端构建，使用已有 dist/
#   -h               显示帮助
#
# 示例:
#   # 部署到所有节点
#   bash deploy_chainbaas.sh -a
#
#   # 只部署到 shard2 节点
#   bash deploy_chainbaas.sh -H 192.168.26.203
#
#   # 跳过构建，直接重新部署
#   bash deploy_chainbaas.sh -a -s
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHAINBAAS_DIR="$SCRIPT_DIR/chainbaas"
REMOTE_DIR="/root/chainbaas"
NGINX_CONF_SRC="$CHAINBAAS_DIR/nginx_chainbaas.conf"
NGINX_CONF_DST="/etc/nginx/conf.d/chainbaas.conf"

# 所有节点
ALL_NODES=(
    "192.168.26.203"
    "192.168.26.213"
    "192.168.26.211"
    "192.168.26.204"
    "192.168.26.215"
)

# 默认参数
TARGET_IPS="192.168.26.203"
DEPLOY_ALL=0
PASSWORD="1"
SSH_PORT=22
SKIP_BUILD=0

usage() { grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

while getopts "H:ap:P:sh" opt; do
    case "$opt" in
        H) TARGET_IPS="$OPTARG" ;;
        a) DEPLOY_ALL=1 ;;
        p) PASSWORD="$OPTARG" ;;
        P) SSH_PORT="$OPTARG" ;;
        s) SKIP_BUILD=1 ;;
        h) usage ;;
        *) usage ;;
    esac
done

if [ "$DEPLOY_ALL" = "1" ]; then
    TARGET_IPS=$(IFS=','; echo "${ALL_NODES[*]}")
fi

IFS=',' read -ra IP_ARRAY <<< "$TARGET_IPS"

echo "============================================================"
echo "  Chainbaas 重新部署"
echo "  源码目录: $CHAINBAAS_DIR"
echo "  目标路径: $REMOTE_DIR"
echo "  目标节点: ${IP_ARRAY[*]}"
echo "  跳过构建: $SKIP_BUILD"
echo "============================================================"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log()  { echo -e "${GREEN}[$(date +%H:%M:%S)] $*${NC}"; }
warn() { echo -e "${YELLOW}[$(date +%H:%M:%S)] WARN: $*${NC}"; }
err()  { echo -e "${RED}[$(date +%H:%M:%S)] ERR: $*${NC}" >&2; exit 1; }

ssh_run() {
    local ip="$1"; shift
    sshpass -p "$PASSWORD" ssh \
        -o ConnectTimeout=15 -o StrictHostKeyChecking=no \
        -o ServerAliveInterval=10 -p "$SSH_PORT" \
        root@"$ip" "$@"
}

scp_to() {
    local ip="$1" src="$2" dst="$3"
    sshpass -p "$PASSWORD" scp \
        -P "$SSH_PORT" -o ConnectTimeout=15 -o StrictHostKeyChecking=no \
        -r "$src" root@"$ip":"$dst"
}

# ── Step 1: 构建前端 ─────────────────────────────────────────
if [ "$SKIP_BUILD" = "0" ]; then
    log "Step 1: 构建 Vue.js 前端..."
    cd "$CHAINBAAS_DIR"
    if ! command -v node >/dev/null 2>&1; then
        err "未找到 Node.js，请先安装 Node.js >= 16"
    fi
    log "  node: $(node --version)  npm: $(npm --version)"
    npm install --prefer-offline 2>&1 | tail -3
    npm run build 2>&1 | tail -10
    cd "$SCRIPT_DIR"
    [ -d "$CHAINBAAS_DIR/dist" ] || err "构建失败: dist/ 目录不存在"
    log "  前端构建完成: $CHAINBAAS_DIR/dist"
else
    [ -d "$CHAINBAAS_DIR/dist" ] || err "dist/ 不存在，请先构建（去掉 -s 选项）"
    log "Step 1: 跳过构建，使用已有 dist/"
fi

# ── Step 2: 打包 ─────────────────────────────────────────────
log "Step 2: 打包部署文件..."
PKG_TAR="/tmp/chainbaas_deploy_$$.tar.gz"
tar -czf "$PKG_TAR" \
    -C "$CHAINBAAS_DIR" \
    dist \
    explorer_proxy.py \
    solc_server.py \
    nginx_chainbaas.conf
log "  打包完成: $PKG_TAR ($(du -sh "$PKG_TAR" | cut -f1))"

# ── Step 3: 部署到各节点 ─────────────────────────────────────
deploy_to_host() {
    local ip="$1"
    log "  -> $ip: 开始部署..."

    # 上传包
    scp_to "$ip" "$PKG_TAR" "/tmp/chainbaas_deploy.tar.gz"

    # 解包到 /root/chainbaas
    ssh_run "$ip" "
        set -e
        mkdir -p ${REMOTE_DIR}
        tar -xzf /tmp/chainbaas_deploy.tar.gz -C ${REMOTE_DIR}/
        rm -f /tmp/chainbaas_deploy.tar.gz
        echo '  解包完成'
    "

    # 安装 nginx（如未安装）
    ssh_run "$ip" "
        if ! command -v nginx >/dev/null 2>&1; then
            echo '  安装 nginx...'
            apt-get update -qq && apt-get install -y -qq nginx
        fi
    "

    # 配置 nginx
    ssh_run "$ip" "
        set -e
        # 复制配置
        cp ${REMOTE_DIR}/nginx_chainbaas.conf /etc/nginx/conf.d/chainbaas.conf
        # 禁用默认站点（如有）
        rm -f /etc/nginx/sites-enabled/default 2>/dev/null || true
        # 移除旧的 chainbaas 软链（如有）
        rm -f /etc/nginx/sites-enabled/chainbaas 2>/dev/null || true
        # 测试配置
        nginx -t
        # 重载/重启
        if systemctl is-active nginx >/dev/null 2>&1; then
            systemctl reload nginx
        else
            systemctl restart nginx
        fi
        echo '  nginx 配置完成并已重启'
    "

    # 安装 Python 依赖并启动后端服务
    ssh_run "$ip" "
        set -e
        cd ${REMOTE_DIR}

        # 停止旧的 Python 服务
        pkill -f 'python3.*solc_server.py' 2>/dev/null || true
        pkill -f 'python3.*explorer_proxy.py' 2>/dev/null || true
        sleep 1

        # 安装 solc（如未安装）
        if ! command -v solc >/dev/null 2>&1; then
            apt-get install -y -qq solc 2>/dev/null || \
            snap install solc 2>/dev/null || \
            echo 'WARN: solc 未安装，Solidity 编译功能不可用'
        fi

        # 启动 solc_server.py（监听 127.0.0.1:18080）
        nohup python3 ${REMOTE_DIR}/solc_server.py \
            > ${REMOTE_DIR}/solc_server.log 2>&1 &
        echo \"  solc_server.py 已启动 (pid \$!)\"

        sleep 1

        # 启动 explorer_proxy.py（监听 0.0.0.0:30302-30306）
        nohup python3 ${REMOTE_DIR}/explorer_proxy.py \
            > ${REMOTE_DIR}/explorer_proxy.log 2>&1 &
        echo \"  explorer_proxy.py 已启动 (pid \$!)\"
    "

    log "  -> $ip: 部署完成"
}

log "Step 3: 部署到 ${#IP_ARRAY[@]} 个节点..."
PIDS=()
for ip in "${IP_ARRAY[@]}"; do
    deploy_to_host "$ip" &
    PIDS+=($!)
done

FAILED=0
for pid in "${PIDS[@]}"; do
    wait "$pid" || { warn "某节点部署失败 (pid=$pid)"; FAILED=1; }
done

rm -f "$PKG_TAR"

# ── 验证 ─────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo "  部署验证"
echo "============================================================"
for ip in "${IP_ARRAY[@]}"; do
    nginx_status=$(sshpass -p "$PASSWORD" ssh \
        -o ConnectTimeout=5 -o StrictHostKeyChecking=no -p "$SSH_PORT" \
        root@"$ip" "systemctl is-active nginx 2>/dev/null || echo inactive" 2>/dev/null || echo "ssh-fail")
    dist_ok=$(sshpass -p "$PASSWORD" ssh \
        -o ConnectTimeout=5 -o StrictHostKeyChecking=no -p "$SSH_PORT" \
        root@"$ip" "[ -f ${REMOTE_DIR}/dist/index.html ] && echo ok || echo missing" 2>/dev/null || echo "ssh-fail")
    printf "  %-18s  nginx=%-10s  dist=%s\n" "$ip" "$nginx_status" "$dist_ok"
done
echo "============================================================"

if [ "$FAILED" = "0" ]; then
    log "所有节点部署完成！"
    echo ""
    echo "  访问地址（各节点）:"
    for ip in "${IP_ARRAY[@]}"; do
        echo "    http://${ip}:8080"
    done
else
    warn "部分节点部署失败，请检查日志"
    exit 1
fi
