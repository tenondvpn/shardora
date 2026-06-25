#!/bin/bash
set -euo pipefail

# ============================================================================
# AWS Lightsail 一键购买、部署、初始化 Shardora 区块链网络
# ============================================================================
#
# 用法:
#   bash aws.sh                    # 完整流程：购买 → 等待 → 配置SSH → 部署
#   bash aws.sh buy                # 仅购买服务器
#   bash aws.sh wait               # 等待所有服务器就绪
#   bash aws.sh ssh-setup          # 配置所有服务器的 root 密码登录
#   bash aws.sh deploy             # 部署并启动节点（需要一台编译服务器）
#   bash aws.sh destroy            # 销毁所有服务器
#   bash aws.sh status             # 查看所有服务器状态
#   bash aws.sh ips                # 输出所有服务器 IP
#
# 前置条件:
#   1. 已安装 aws cli 并配置好凭证 (aws configure)
#   2. 本机有 sshpass (brew install hudochenkov/sshpass/sshpass)
#
# 说明:
#   - 编译在第一台服务器上完成（不需要本地 Linux 环境）
#   - ssh-setup 通过 Lightsail API 获取默认密钥，配置 root 密码登录
# ============================================================================

# ── 全局配置 ─────────────────────────────────────────────────────────────────

REGIONS=(
    "us-east-1" "us-east-2" "us-west-2"
    "eu-central-1" "eu-west-1" "eu-west-2" "eu-west-3" "eu-north-1"
    "ap-south-1" "ap-southeast-1" "ap-southeast-2" "ap-northeast-1"
    "ap-northeast-2" "ca-central-1"
)

NODES_PER_REGION=2
BUNDLE_ID="large_3_0"
BLUEPRINT_ID="ubuntu_24_04"
PASSWORD='Xf4aGbTaf&'
PROJECT_TAG="Shardora-Global"
INSTANCE_PREFIX="Shardora-Node"

EACH_NODES_COUNT=4
END_SHARD=3
BUILD_TARGET="Release"

# shardora 源码 git 仓库（用于在远程服务器上 clone）
SHARDORA_GIT_REPO="${SHARDORA_GIT_REPO:-}"  # 留空则用 scp 上传

IP_CACHE_FILE="/tmp/shardora_aws_ips.txt"
KEY_CACHE_DIR="/tmp/shardora_aws_keys"

# ── 辅助函数 ─────────────────────────────────────────────────────────────────

log() { echo "[$(date '+%H:%M:%S')] $*"; }
err() { echo "[$(date '+%H:%M:%S')] ERROR: $*" >&2; }

wait_sshpass() {
    while pgrep -f "sshpass.*ConnectTimeout" >/dev/null 2>&1; do sleep 1; done
}

# 用 root 密码 SSH
rssh() {
    local ip=$1; shift
    sshpass -p "$PASSWORD" ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
        -o ServerAliveInterval=10 -o ServerAliveCountMax=3 root@"$ip" "$@"
}

rssh_bg() {
    local ip=$1; shift
    sshpass -p "$PASSWORD" ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
        -o ServerAliveInterval=10 root@"$ip" "$@" &
}

rscp() {
    local src=$1 ip=$2 dst=$3
    sshpass -p "$PASSWORD" scp -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
        "$src" root@"$ip":"$dst" &
}

# 用 Lightsail 默认密钥 SSH（ssh-setup 阶段用）
lssh() {
    local ip=$1 region=$2 name=$3; shift 3
    local keyfile="${KEY_CACHE_DIR}/${name}.pem"
    ssh -i "$keyfile" -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
        -o ServerAliveInterval=5 ubuntu@"$ip" "$@"
}

get_ips_from_cache() {
    if [ ! -f "$IP_CACHE_FILE" ]; then
        err "IP 缓存不存在，请先运行: bash aws.sh wait"
        exit 1
    fi
    cat "$IP_CACHE_FILE"
}

# ── 阶段 1：购买服务器 ──────────────────────────────────────────────────────

cmd_buy() {
    log "=== 阶段 1：购买 ${#REGIONS[@]} × ${NODES_PER_REGION} = $((${#REGIONS[@]} * NODES_PER_REGION)) 台服务器 ==="

    for REGION in "${REGIONS[@]}"; do
        # 自动检测该区域的 large bundle ID（不同区域后缀不同）
        local region_bundle
        region_bundle=$(aws lightsail get-bundles --region "$REGION" --no-cli-pager 2>/dev/null \
            | python3 -c "
import sys,json
d=json.load(sys.stdin)
for b in d['bundles']:
    if b['bundleId'].startswith('large_3_') and 'win' not in b['bundleId'] and 'ipv6' not in b['bundleId']:
        print(b['bundleId']); break
" 2>/dev/null || echo "")
        [ -z "$region_bundle" ] && region_bundle="$BUNDLE_ID"

        for ((n=1; n<=NODES_PER_REGION; n++)); do
            local name="${INSTANCE_PREFIX}-${REGION}-${n}"
            log "  创建 $name ($REGION, $region_bundle)..."

            aws lightsail create-instances \
                --region "$REGION" \
                --instance-names "$name" \
                --availability-zone "${REGION}a" \
                --blueprint-id "$BLUEPRINT_ID" \
                --bundle-id "$region_bundle" \
                --tags "key=Project,value=$PROJECT_TAG" \
                --no-cli-pager 2>/dev/null || {
                    err "  创建 $name 失败（可能已存在）"
                    continue
                }

            aws lightsail put-instance-public-ports \
                --region "$REGION" \
                --instance-name "$name" \
                --port-infos \
                    "fromPort=0,toPort=65535,protocol=tcp" \
                    "fromPort=0,toPort=65535,protocol=udp" \
                --no-cli-pager 2>/dev/null || true

            log "  ✓ $name"
        done
    done
    log "=== 购买完毕 ==="
}

# ── 阶段 2：等待就绪 + 收集 IP ──────────────────────────────────────────────

cmd_wait() {
    log "=== 阶段 2：等待服务器就绪 ==="
    local total=$((${#REGIONS[@]} * NODES_PER_REGION))
    local max_wait=900
    local start_time=$SECONDS
    local ready=0 all_ips=""

    while [ $ready -lt $total ] && [ $((SECONDS - start_time)) -lt $max_wait ]; do
        ready=0; all_ips=""
        for REGION in "${REGIONS[@]}"; do
            for ((n=1; n<=NODES_PER_REGION; n++)); do
                local name="${INSTANCE_PREFIX}-${REGION}-${n}"
                local info
                info=$(aws lightsail get-instance --region "$REGION" \
                    --instance-name "$name" --no-cli-pager 2>/dev/null || echo '{}')
                local state ip
                state=$(echo "$info" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('instance',{}).get('state',{}).get('name','?'))" 2>/dev/null || echo "?")
                ip=$(echo "$info" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('instance',{}).get('publicIpAddress',''))" 2>/dev/null || echo "")
                if [ "$state" = "running" ] && [ -n "$ip" ]; then
                    ready=$((ready + 1))
                    [ -n "$all_ips" ] && all_ips="${all_ips},"
                    all_ips="${all_ips}${ip}"
                fi
            done
        done
        log "  [$((SECONDS - start_time))s] 就绪: $ready/$total"
        [ $ready -lt $total ] && sleep 15
    done

    [ $ready -lt $total ] && err "超时：$ready/$total 就绪（继续使用已就绪的服务器）"
    echo "$all_ips" > "$IP_CACHE_FILE"
    log "  IP: $all_ips"
    log "=== 等待完成 ==="
}

# ── 阶段 3：配置 SSH root 密码登录 ──────────────────────────────────────────

cmd_ssh_setup() {
    log "=== 阶段 3：配置 SSH root 密码登录 ==="
    mkdir -p "$KEY_CACHE_DIR"

    # 确保 setup 脚本存在
    local setup_script
    setup_script="$(cd "$(dirname "$0")" && pwd)/setup_root_ssh.sh"
    if [ ! -f "$setup_script" ]; then
        err "找不到 $setup_script"
        exit 1
    fi

    local configured=0 failed=0

    for REGION in "${REGIONS[@]}"; do
        # 每个区域下载一次密钥
        local region_keyfile="${KEY_CACHE_DIR}/region-${REGION}.pem"
        if [ ! -f "$region_keyfile" ]; then
            aws lightsail download-default-key-pair --region "$REGION" --no-cli-pager 2>/dev/null \
                | python3 -c "import sys,json; print(json.load(sys.stdin)['privateKeyBase64'])" \
                > "$region_keyfile" 2>/dev/null
            if [ ! -s "$region_keyfile" ] || ! grep -q "BEGIN" "$region_keyfile"; then
                err "  $REGION: 无法下载密钥"
                rm -f "$region_keyfile"
                failed=$((failed + NODES_PER_REGION))
                continue
            fi
            chmod 600 "$region_keyfile"
        fi

        for ((n=1; n<=NODES_PER_REGION; n++)); do
            local name="${INSTANCE_PREFIX}-${REGION}-${n}"
            local ip
            ip=$(aws lightsail get-instance --region "$REGION" \
                --instance-name "$name" --no-cli-pager 2>/dev/null \
                | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['instance']['publicIpAddress'])" 2>/dev/null || echo "")
            [ -z "$ip" ] && { err "  $name: 无 IP"; failed=$((failed+1)); continue; }

            log "  配置 $name ($ip)..."
            if ssh -i "$region_keyfile" -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
                ubuntu@"$ip" "sudo bash -s" < "$setup_script" 2>&1 | grep -q "SSH_SETUP_OK"; then
                configured=$((configured + 1))
                log "  ✓ $name ($ip)"
            else
                failed=$((failed + 1))
                err "  ✗ $name ($ip)"
            fi
        done
    done

    # 验证 root 密码登录
    log "  验证 root 密码登录..."
    local all_ips
    all_ips=$(get_ips_from_cache)
    local ips_array
    IFS=',' read -ra ips_array <<< "$all_ips"
    local verify_ok=0
    for ip in "${ips_array[@]}"; do
        if sshpass -p "$PASSWORD" ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no \
            root@"$ip" "echo ok" >/dev/null 2>&1; then
            verify_ok=$((verify_ok + 1))
        else
            err "  root 登录失败: $ip"
        fi
    done
    log "=== SSH 配置完成: 配置=$configured, 失败=$failed, 可登录=$verify_ok/${#ips_array[@]} ==="
}

# ── 阶段 4：部署（在第一台服务器上编译，分发到所有服务器）────────────────────

cmd_deploy() {
    log "=== 阶段 4：部署 Shardora 网络 ==="

    local all_ips
    all_ips=$(get_ips_from_cache)
    local ips_array
    IFS=',' read -ra ips_array <<< "$all_ips"
    local server_count=${#ips_array[@]}
    local total_nodes=$((server_count * EACH_NODES_COUNT))
    local build_server="${ips_array[0]}"

    log "  服务器: $server_count 台, 节点: $total_nodes"
    log "  编译服务器: $build_server"

    # 4.1 上传源码到编译服务器
    log "  [4.1] 上传源码到编译服务器..."
    local src_dir
    src_dir="$(cd "$(dirname "$0")/.." && pwd)"

    # 打包源码（排除 build 目录和 .git）
    log "  打包源码..."
    cd "$src_dir"
    tar --exclude='./cbuild_*' --exclude='./.git' --exclude='./nodes_local/*/db*' \
        -zcf /tmp/shardora_src.tar.gz . 2>/dev/null

    log "  上传到 $build_server..."
    sshpass -p "$PASSWORD" scp -o ConnectTimeout=30 -o StrictHostKeyChecking=no \
        /tmp/shardora_src.tar.gz root@"$build_server":/root/
    rm -f /tmp/shardora_src.tar.gz

    # 4.2 在编译服务器上编译 + 生成密钥 + 打包
    log "  [4.2] 远程编译 + 打包（可能需要几分钟）..."
    rssh "$build_server" "bash -s" <<REMOTE_BUILD
set -e
cd /root
mkdir -p /root/shardora
cd /root/shardora
tar -zxf /root/shardora_src.tar.gz

# 安装编译依赖（如果需要）
which cmake >/dev/null 2>&1 || apt-get update -qq && apt-get install -y -qq cmake g++ make libssl-dev 2>/dev/null

# 编译
bash build.sh a ${BUILD_TARGET}
cd cbuild_${BUILD_TARGET} && make -j\$(nproc) txcli 2>/dev/null || true
cd /root/shardora

# 生成密钥和创世块
rm -rf /root/nodes
cp -rf ./nodes_local /root/nodes
rm -rf /root/nodes/*/shardora /root/nodes/*/core* /root/nodes/*/log/* /root/nodes/*/*db*
cp -rf ./nodes_local/shardora/conf/GeoLite2-City.mmdb /root/nodes/shardora
cp -rf ./nodes_local/shardora/conf/log4cpp.properties /root/nodes/shardora/conf
mkdir -p /root/nodes/shardora/log
cp -rf ./cbuild_${BUILD_TARGET}/shardora /root/nodes/shardora

cd /root/nodes/shardora
./shardora -U -N ${total_nodes} -E 4
./shardora -S 3 -N ${total_nodes} -E 4
./shardora -C

# 打包
mkdir -p /root/nodes/shardora/pkg
cp /root/nodes/shardora/shardora /root/nodes/shardora/pkg
cp /root/nodes/shardora/conf/GeoLite2-City.mmdb /root/nodes/shardora/pkg
cp /root/nodes/shardora/conf/log4cpp.properties /root/nodes/shardora/pkg
cp /root/shardora/temp_cmd.sh /root/nodes/shardora/pkg
cp /root/shardora/start_cmd.sh /root/nodes/shardora/pkg
cp -rf /root/nodes/shardora/shard_db_2 /root/nodes/shardora/pkg
cp -rf /root/nodes/shardora/shard_db_3 /root/nodes/shardora/pkg
cp -rf /root/nodes/temp /root/nodes/shardora/pkg
cp /root/shardora/cbuild_${BUILD_TARGET}/txcli /root/nodes/shardora/pkg 2>/dev/null || true

for shard_id in \$(seq 2 ${END_SHARD}); do
    /root/shardora/cbuild_${BUILD_TARGET}/shardora -A /root/shardora/shards\${shard_id} -D /root/nodes/shardora/pkg/shards\${shard_id} 2>/dev/null || \
    cp /root/shardora/shards\${shard_id} /root/nodes/shardora/pkg/shards\${shard_id}
    /root/shardora/cbuild_${BUILD_TARGET}/shardora -A /root/shardora/init_accounts\${shard_id} -D /root/nodes/shardora/pkg/init_accounts\${shard_id} 2>/dev/null || \
    cp /root/shardora/init_accounts\${shard_id} /root/nodes/shardora/pkg/init_accounts\${shard_id}
done

echo "BUILD_OK"
REMOTE_BUILD

    # 4.3 生成 bootstrap 并写入配置（在编译服务器上执行）
    log "  [4.3] 生成 bootstrap..."
    rssh "$build_server" "bash -s" <<REMOTE_BOOTSTRAP
set -e
cd /root/nodes/shardora

# 生成 bootstrap 字符串
bootstrap=""
$(
    for ((shard_id=2; shard_id<=END_SHARD; shard_id++)); do
        node_idx=1
        for ip in "${ips_array[@]}"; do
            for ((j=0; j<EACH_NODES_COUNT; j++)); do
                cat <<BSLINE
pubkey=\$(sed -n "${node_idx}p" /root/nodes/shardora/pkg/shards${shard_id} | awk -F'\t' '{print \$2}')
port=${node_idx}
if [ \$port -ge 100 ]; then p="1${shard_id}\${port}"; elif [ \$port -ge 10 ]; then p="1${shard_id}0\${port}"; else p="1${shard_id}00\${port}"; fi
[ \$p -gt 65535 ] && p=\$(( (p % 60000) + 1024 ))
bootstrap="\${bootstrap},\${pubkey}:${ip}:\${p}:${shard_id}"
BSLINE
                node_idx=$((node_idx + 1))
            done
        done
    done
)

printf "%s" "\$bootstrap" > /tmp/bootstrap_data.tmp
python3 -c "
conf_path = '/root/nodes/shardora/pkg/temp/conf/shardora.conf'
with open('/tmp/bootstrap_data.tmp', 'r') as f:
    new_val = f.read()
with open(conf_path, 'r') as f:
    content = f.read()
with open(conf_path, 'w') as f:
    f.write(content.replace('BOOTSTRAP', new_val))
"
rm -f /tmp/bootstrap_data.tmp

cd /root/nodes/shardora/
tar -zcf pkg.tar.gz ./pkg
echo "BOOTSTRAP_OK"
REMOTE_BOOTSTRAP

    # 4.4 从编译服务器分发到所有服务器
    log "  [4.4] 分发部署包..."
    local batch=0
    for ip in "${ips_array[@]}"; do
        if [ "$ip" = "$build_server" ]; then
            # 编译服务器已有 pkg.tar.gz
            rssh_bg "$ip" "cp /root/nodes/shardora/pkg.tar.gz /root/"
        else
            # 从编译服务器 scp 到其他服务器
            rssh_bg "$build_server" "sshpass -p '${PASSWORD}' scp -o StrictHostKeyChecking=no /root/nodes/shardora/pkg.tar.gz root@${ip}:/root/"
        fi
        batch=$((batch + 1))
        ((batch >= 30)) && { wait_sshpass; batch=0; }
    done
    wait_sshpass

    # 确保所有服务器有 sshpass（用于服务器间传输）
    log "  安装 sshpass..."
    for ip in "${ips_array[@]}"; do
        rssh_bg "$ip" "which sshpass >/dev/null 2>&1 || apt-get install -y -qq sshpass 2>/dev/null"
        batch=$((batch + 1))
        ((batch >= 30)) && { wait_sshpass; batch=0; }
    done
    wait_sshpass

    # 4.5 配置节点
    log "  [4.5] 配置节点..."
    local start_pos=1
    local leader_init_tm
    leader_init_tm=$(python3 -c "import time; print(int(time.time()) + 240*86400)")
    batch=0
    local first=true
    for ip in "${ips_array[@]}"; do
        rssh_bg "$ip" "cd /root && tar -zxf pkg.tar.gz && cd ./pkg && bash temp_cmd.sh $ip $start_pos $EACH_NODES_COUNT 0 2 $END_SHARD $leader_init_tm"
        if $first; then sleep 3; first=false; fi
        batch=$((batch + 1))
        ((batch >= 50)) && { wait_sshpass; batch=0; }
        start_pos=$((start_pos + EACH_NODES_COUNT))
    done
    wait_sshpass

    # 4.6 启动节点
    log "  [4.6] 启动节点..."
    start_pos=1
    first=true
    for ip in "${ips_array[@]}"; do
        rssh_bg "$ip" "cd /root/pkg && bash start_cmd.sh $ip $start_pos $EACH_NODES_COUNT 0 2 $END_SHARD"
        if $first; then sleep 3; first=false; fi
        sleep 0.2
        start_pos=$((start_pos + EACH_NODES_COUNT))
    done
    wait_sshpass

    log "=== 部署完成 ==="
    log "  服务器: $server_count, 节点: $total_nodes"
    log "  第一个节点: https://${build_server}:23001"
    log "  登录: root / $PASSWORD"
}

# ── 销毁 ─────────────────────────────────────────────────────────────────────

cmd_destroy() {
    log "=== 销毁所有 Shardora 服务器 ==="
    read -p "确认销毁？(yes/no): " confirm
    [ "$confirm" != "yes" ] && { log "已取消"; exit 0; }

    for REGION in "${REGIONS[@]}"; do
        for ((n=1; n<=NODES_PER_REGION; n++)); do
            local name="${INSTANCE_PREFIX}-${REGION}-${n}"
            aws lightsail delete-instance --region "$REGION" \
                --instance-name "$name" --no-cli-pager 2>/dev/null || true
            log "  删除 $name"
        done
    done
    rm -f "$IP_CACHE_FILE"
    rm -rf "$KEY_CACHE_DIR"
    log "=== 销毁完成 ==="
}

# ── 状态 ─────────────────────────────────────────────────────────────────────

cmd_status() {
    printf "%-35s %-15s %-16s %-10s\n" "INSTANCE" "REGION" "IP" "STATE"
    printf '%.0s-' {1..80}; echo
    for REGION in "${REGIONS[@]}"; do
        for ((n=1; n<=NODES_PER_REGION; n++)); do
            local name="${INSTANCE_PREFIX}-${REGION}-${n}"
            local info state ip
            info=$(aws lightsail get-instance --region "$REGION" --instance-name "$name" --no-cli-pager 2>/dev/null || echo '{}')
            state=$(echo "$info" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('instance',{}).get('state',{}).get('name','?'))" 2>/dev/null || echo "?")
            ip=$(echo "$info" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('instance',{}).get('publicIpAddress',''))" 2>/dev/null || echo "")
            printf "%-35s %-15s %-16s %-10s\n" "$name" "$REGION" "${ip:-N/A}" "$state"
        done
    done
}

# ── IP 列表 ──────────────────────────────────────────────────────────────────

cmd_ips() {
    local all_ips=""
    for REGION in "${REGIONS[@]}"; do
        for ((n=1; n<=NODES_PER_REGION; n++)); do
            local name="${INSTANCE_PREFIX}-${REGION}-${n}"
            local ip
            ip=$(aws lightsail get-instance --region "$REGION" --instance-name "$name" --no-cli-pager 2>/dev/null \
                | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['instance']['publicIpAddress'])" 2>/dev/null || echo "")
            [ -n "$ip" ] && { [ -n "$all_ips" ] && all_ips="${all_ips},"; all_ips="${all_ips}${ip}"; }
        done
    done
    echo "$all_ips"
    echo "$all_ips" > "$IP_CACHE_FILE"
}

# ── 主入口 ───────────────────────────────────────────────────────────────────

ACTION="${1:-all}"

case "$ACTION" in
    buy)       cmd_buy ;;
    wait)      cmd_wait ;;
    ssh-setup) cmd_ssh_setup ;;
    deploy)    cmd_deploy ;;
    destroy)   cmd_destroy ;;
    status)    cmd_status ;;
    ips)       cmd_ips ;;
    all)
        cmd_buy
        cmd_wait
        cmd_ssh_setup
        cmd_deploy
        log ""
        log "============================================"
        log "  Shardora 全球网络部署完成！"
        log "  服务器: $((${#REGIONS[@]} * NODES_PER_REGION)) 台"
        log "  节点数: $((${#REGIONS[@]} * NODES_PER_REGION * EACH_NODES_COUNT))"
        log "  登录: root / $PASSWORD"
        log "============================================"
        ;;
    *)
        echo "用法: bash aws.sh [buy|wait|ssh-setup|deploy|destroy|status|ips|all]"
        exit 1
        ;;
esac
