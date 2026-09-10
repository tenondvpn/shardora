#!/bin/bash
# Deploy a 5-shard Shardora network + blockchain explorer on a single machine
#
# Usage:
#   ./scripts/deploy_5shard_explorer.sh [options]
#
# Options:
#   --server-ip IP      Public IP of the server (default: 139.159.119.119)
#   --source-dir DIR    Path to shardora source (default: script's parent dir)
#   --deploy-dir DIR    Where nodes are installed (default: /root/shardoras)
#   --skip-build        Don't recompile the shardora binary
#   --skip-genesis      Don't regenerate key files / genesis DBs (use existing)
#   --skip-frontend     Don't rebuild chainbaas Vue frontend
#   --restart           Kill existing nodes and restart them
#   --stop              Stop all nodes and exit
#   --status            Print node status and exit
#
# Network layout (10 nodes, 2 per shard):
#   Root shard (net_id=2):  r1 P2P=12001  |  r2 P2P=12002 HTTP=30302
#   Shard 3    (net_id=3): s3_1 P2P=13001  | s3_2 P2P=13002 HTTP=30303
#   Shard 4    (net_id=4): s4_1 P2P=14001  | s4_2 P2P=14002 HTTP=30304
#   Shard 5    (net_id=5): s5_1 P2P=15001  | s5_2 P2P=15002 HTTP=30305
#   Shard 6    (net_id=6): s6_1 P2P=16001  | s6_2 P2P=16002 HTTP=30306
#
# Nginx exposes explorer nodes as plain HTTP:
#   40302→30302 (root), 40303→30303 (s3), 40304→30304 (s4),
#   40305→30305 (s5), 40306→30306 (s6)
#
# Frontend served at http://SERVER_IP:80

set -euo pipefail

# ──────────────────────────────────────────────────────────────
# Defaults
# ──────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_IP="139.159.119.119"
DEPLOY_DIR="/root/shardoras"
CHAINBAAS_DIR="$SOURCE_DIR/chainbaas"
WEB_ROOT="/var/www/chainbaas"
NODE_COUNT=2      # nodes per shard
END_SHARD_ID=7    # shards 2..6  (root + shards 3,4,5,6)

SKIP_BUILD=0
SKIP_GENESIS=0
SKIP_FRONTEND=0
DO_RESTART=0
DO_STOP=0
DO_STATUS=0

# ──────────────────────────────────────────────────────────────
# Parse args
# ──────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case $1 in
    --server-ip)    SERVER_IP="$2";    shift 2 ;;
    --source-dir)   SOURCE_DIR="$2";   shift 2 ;;
    --deploy-dir)   DEPLOY_DIR="$2";   shift 2 ;;
    --skip-build)   SKIP_BUILD=1;      shift   ;;
    --skip-genesis) SKIP_GENESIS=1;    shift   ;;
    --skip-frontend)SKIP_FRONTEND=1;   shift   ;;
    --restart)      DO_RESTART=1;      shift   ;;
    --stop)         DO_STOP=1;         shift   ;;
    --status)       DO_STATUS=1;       shift   ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

BINARY="$SOURCE_DIR/cbuild_Release/shardora"
GENESIS_DIR="$DEPLOY_DIR/genesis"
CERTS_DIR="$DEPLOY_DIR/certs"

# ──────────────────────────────────────────────────────────────
# Logging helpers
# ──────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
log()    { echo -e "${GREEN}[$(date +%H:%M:%S)] $*${NC}"; }
info()   { echo -e "${CYAN}[$(date +%H:%M:%S)] $*${NC}"; }
warn()   { echo -e "${YELLOW}[$(date +%H:%M:%S)] WARN: $*${NC}"; }
err()    { echo -e "${RED}[$(date +%H:%M:%S)] ERR: $*${NC}" >&2; exit 1; }
section(){ echo -e "\n${YELLOW}════════ $* ════════${NC}"; }

# ──────────────────────────────────────────────────────────────
# Node layout tables
# shard_ids: root=2, s3=3, s4=4, s5=5, s6=6
# node_names: r1, r2, s3_1, s3_2, s4_1, s4_2, s5_1, s5_2, s6_1, s6_2
# ──────────────────────────────────────────────────────────────
declare -A NODE_SHARD   # node_name -> net_id
declare -A NODE_P2P     # node_name -> p2p port
declare -A NODE_HTTP    # node_name -> http port (0=disabled)
declare -A NODE_EXPLORER# node_name -> 1 if explorer node
declare -A NODE_ROOT    # node_name -> 1 if root shard node
declare -A NODE_KEYFILE # node_name -> key file name (root_nodes or shards3 etc)
declare -A NODE_DBDIR   # node_name -> genesis DB dir name (root_db or shard_db_3 etc)

setup_layout() {
  # Root shard
  NODE_SHARD[r1]=2;   NODE_P2P[r1]=12001; NODE_HTTP[r1]=0;     NODE_EXPLORER[r1]=0; NODE_ROOT[r1]=1; NODE_KEYFILE[r1]=root_nodes; NODE_DBDIR[r1]=root_db
  NODE_SHARD[r2]=2;   NODE_P2P[r2]=12002; NODE_HTTP[r2]=30302; NODE_EXPLORER[r2]=1; NODE_ROOT[r2]=1; NODE_KEYFILE[r2]=root_nodes; NODE_DBDIR[r2]=root_db
  # Shard 3
  NODE_SHARD[s3_1]=3; NODE_P2P[s3_1]=13001; NODE_HTTP[s3_1]=0;     NODE_EXPLORER[s3_1]=0; NODE_ROOT[s3_1]=0; NODE_KEYFILE[s3_1]=shards3; NODE_DBDIR[s3_1]=shard_db_3
  NODE_SHARD[s3_2]=3; NODE_P2P[s3_2]=13002; NODE_HTTP[s3_2]=30303; NODE_EXPLORER[s3_2]=1; NODE_ROOT[s3_2]=0; NODE_KEYFILE[s3_2]=shards3; NODE_DBDIR[s3_2]=shard_db_3
  # Shard 4
  NODE_SHARD[s4_1]=4; NODE_P2P[s4_1]=14001; NODE_HTTP[s4_1]=0;     NODE_EXPLORER[s4_1]=0; NODE_ROOT[s4_1]=0; NODE_KEYFILE[s4_1]=shards4; NODE_DBDIR[s4_1]=shard_db_4
  NODE_SHARD[s4_2]=4; NODE_P2P[s4_2]=14002; NODE_HTTP[s4_2]=30304; NODE_EXPLORER[s4_2]=1; NODE_ROOT[s4_2]=0; NODE_KEYFILE[s4_2]=shards4; NODE_DBDIR[s4_2]=shard_db_4
  # Shard 5
  NODE_SHARD[s5_1]=5; NODE_P2P[s5_1]=15001; NODE_HTTP[s5_1]=0;     NODE_EXPLORER[s5_1]=0; NODE_ROOT[s5_1]=0; NODE_KEYFILE[s5_1]=shards5; NODE_DBDIR[s5_1]=shard_db_5
  NODE_SHARD[s5_2]=5; NODE_P2P[s5_2]=15002; NODE_HTTP[s5_2]=30305; NODE_EXPLORER[s5_2]=1; NODE_ROOT[s5_2]=0; NODE_KEYFILE[s5_2]=shards5; NODE_DBDIR[s5_2]=shard_db_5
  # Shard 6
  NODE_SHARD[s6_1]=6; NODE_P2P[s6_1]=16001; NODE_HTTP[s6_1]=0;     NODE_EXPLORER[s6_1]=0; NODE_ROOT[s6_1]=0; NODE_KEYFILE[s6_1]=shards6; NODE_DBDIR[s6_1]=shard_db_6
  NODE_SHARD[s6_2]=6; NODE_P2P[s6_2]=16002; NODE_HTTP[s6_2]=30306; NODE_EXPLORER[s6_2]=1; NODE_ROOT[s6_2]=0; NODE_KEYFILE[s6_2]=shards6; NODE_DBDIR[s6_2]=shard_db_6
}

ALL_NODES=(r1 r2 s3_1 s3_2 s4_1 s4_2 s5_1 s5_2 s6_1 s6_2)
# Key index: r1=line1, r2=line2; s3_1=line1, s3_2=line2; etc.
declare -A NODE_KEYLINE
NODE_KEYLINE[r1]=1;   NODE_KEYLINE[r2]=2
NODE_KEYLINE[s3_1]=1; NODE_KEYLINE[s3_2]=2
NODE_KEYLINE[s4_1]=1; NODE_KEYLINE[s4_2]=2
NODE_KEYLINE[s5_1]=1; NODE_KEYLINE[s5_2]=2
NODE_KEYLINE[s6_1]=1; NODE_KEYLINE[s6_2]=2

# ──────────────────────────────────────────────────────────────
# Stop helpers
# ──────────────────────────────────────────────────────────────
stop_nodes() {
  log "Stopping all shardora nodes..."
  for name in "${ALL_NODES[@]}"; do
    local dir="$DEPLOY_DIR/$name"
    local pidfile="$dir/shardora.pid"
    if [[ -f "$pidfile" ]]; then
      local pid; pid=$(cat "$pidfile")
      if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" && log "  Stopped $name (pid $pid)"
      fi
      rm -f "$pidfile"
    fi
  done
  # Fallback: kill any lingering shardora processes started from DEPLOY_DIR
  pkill -f "$DEPLOY_DIR" 2>/dev/null || true
  log "All nodes stopped."
}

node_status() {
  section "Node Status"
  printf "%-8s %-6s %-7s %-7s %-9s %-10s\n" "Node" "Shard" "P2P" "HTTP" "Explorer" "Status"
  printf "%-8s %-6s %-7s %-7s %-9s %-10s\n" "----" "-----" "---" "----" "--------" "------"
  for name in "${ALL_NODES[@]}"; do
    local dir="$DEPLOY_DIR/$name"
    local pidfile="$dir/shardora.pid"
    local status="stopped"
    if [[ -f "$pidfile" ]]; then
      local pid; pid=$(cat "$pidfile")
      kill -0 "$pid" 2>/dev/null && status="running(${pid})" || status="dead(${pid})"
    fi
    local explorer_flag=""
    [[ "${NODE_EXPLORER[$name]}" == "1" ]] && explorer_flag="yes"
    printf "%-8s %-6s %-7s %-7s %-9s %-10s\n" \
      "$name" "${NODE_SHARD[$name]}" "${NODE_P2P[$name]}" "${NODE_HTTP[$name]}" \
      "$explorer_flag" "$status"
  done
}

# ──────────────────────────────────────────────────────────────
# Build
# ──────────────────────────────────────────────────────────────
build_shardora() {
  section "Building Shardora"
  [[ -d "$SOURCE_DIR" ]] || err "Source dir not found: $SOURCE_DIR"
  cd "$SOURCE_DIR"
  if [[ -f "build.sh" ]]; then
    log "Running build.sh Release..."
    bash build.sh a Release 2>&1 | tail -20
  else
    log "Running cmake build..."
    mkdir -p cbuild_Release && cd cbuild_Release
    cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_TESTS=OFF
    make -j"$(nproc)"
    cd ..
  fi
  [[ -x "$BINARY" ]] || err "Build failed: binary not found at $BINARY"
  log "Build complete: $BINARY"
}

# ──────────────────────────────────────────────────────────────
# SSL cert generation
# ──────────────────────────────────────────────────────────────
gen_certs() {
  section "SSL Certificates"
  if [[ -f "$CERTS_DIR/server-cert.pem" && -f "$CERTS_DIR/server-key.pem" ]]; then
    log "Certs already exist — skipping (delete $CERTS_DIR to regenerate)"
    return
  fi
  mkdir -p "$CERTS_DIR"
  log "Generating self-signed cert (IP SAN: 127.0.0.1, $SERVER_IP)..."
  openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 \
    -nodes \
    -keyout "$CERTS_DIR/server-key.pem" \
    -out    "$CERTS_DIR/server-cert.pem" \
    -subj   "/CN=shardora/O=Shardora/C=CN" \
    -addext "subjectAltName=IP:127.0.0.1,IP:$SERVER_IP" \
    2>/dev/null
  log "Cert written to $CERTS_DIR/"
}

# ──────────────────────────────────────────────────────────────
# Genesis
# ──────────────────────────────────────────────────────────────
run_genesis() {
  section "Genesis"
  local need_genesis=0
  if [[ ! -f "$GENESIS_DIR/root_nodes" ]]; then
    need_genesis=1
  fi
  for shard in 3 4 5 6; do
    [[ ! -f "$GENESIS_DIR/shards${shard}" ]] && need_genesis=1
  done

  if [[ "$need_genesis" == "0" ]]; then
    log "Genesis files already exist — skipping (delete $GENESIS_DIR to regenerate)"
    return
  fi

  [[ -x "$BINARY" ]] || err "Binary not found: $BINARY (run without --skip-build)"

  mkdir -p "$GENESIS_DIR"
  local prev_dir="$PWD"
  cd "$GENESIS_DIR"

  log "Running genesis: -U -N $NODE_COUNT -E $END_SHARD_ID  (creates key files)"
  "$BINARY" -U -N "$NODE_COUNT" -E "$END_SHARD_ID" || err "Genesis -U failed"

  log "Running genesis: -S -N $NODE_COUNT -E $END_SHARD_ID  (creates genesis DBs)"
  "$BINARY" -S -N "$NODE_COUNT" -E "$END_SHARD_ID" || err "Genesis -S failed"

  cd "$prev_dir"

  log "Genesis complete. Files in $GENESIS_DIR:"
  ls -la "$GENESIS_DIR/" | grep -v '^total' | grep -v '^d.*\.$'
}

# ──────────────────────────────────────────────────────────────
# Read private/public key for a node
# ──────────────────────────────────────────────────────────────
get_prikey() { # args: keyfile line
  awk -F'\t' '{print $1}' < <(sed -n "${2}p" "$GENESIS_DIR/$1")
}
get_pubkey() { # args: keyfile line
  awk -F'\t' '{print $2}' < <(sed -n "${2}p" "$GENESIS_DIR/$1")
}

# ──────────────────────────────────────────────────────────────
# Build bootstrap string from all node 1s in each shard
# ──────────────────────────────────────────────────────────────
build_bootstrap() {
  local bootstrap=""
  # Root shard node 1
  local r1_pub; r1_pub=$(get_pubkey root_nodes 1)
  bootstrap="${r1_pub}:127.0.0.1:12001"
  # Shard 3-6 node 1
  for shard in 3 4 5 6; do
    local pub; pub=$(get_pubkey "shards${shard}" 1)
    local port="1${shard}001"
    bootstrap="${bootstrap},${pub}:127.0.0.1:${port}"
  done
  echo "$bootstrap"
}

# ──────────────────────────────────────────────────────────────
# Write node config
# ──────────────────────────────────────────────────────────────
write_node_config() {
  local node_dir="$1"
  local net_id="$2"
  local local_port="$3"
  local http_port="$4"
  local prikey="$5"
  local bootstrap="$6"
  local is_root="${7:-0}"      # 1 = root shard node (join_root=0)
  local for_explorer="${8:-0}" # 1 = enable explorer SQLite

  local join_root=1
  [[ "$is_root" == "1" ]] && join_root=0

  local for_explorer_val=0
  [[ "$for_explorer" == "1" ]] && for_explorer_val=1

  mkdir -p "$node_dir/conf" "$node_dir/db" "$node_dir/log"

  cat > "$node_dir/conf/shardora.conf" <<EOF
[db]
path = "./db"

[log]
path = "log/shardora.log"

[shardora]
bootstrap = ${bootstrap}
bootstrap_net =
consensus_thread_count = 16
each_shard_max_members = 1024
sharding_min_nodes_count = 2
join_root = ${join_root}
country = "NL"
first_node = 0
http_port = ${http_port}
local_ip = 127.0.0.1
local_port = ${local_port}
public_ip = ${SERVER_IP}
public_port = ${local_port}
net_id = ${net_id}
node_tag =
prikey = ${prikey}
show_cmd = 0
for_ck = 0
for_explorer = ${for_explorer_val}
root_path = .
server_cert_path = ./server-cert.pem
server_key_path = ./server-key.pem
missing_node = 0
ck_host = 127.0.0.1
ck_port = 9000
ck_user = default
ck_pass =
ip_db_path = ./conf/GeoLite2-City.mmdb
tx_user_qps_limit_window = 102400
tx_user_qps_limit_window_sconds = 1
each_tx_pool_max_txs = 40960
test_pool_index = -1
test_tx_tps = 0
leader_change_init_tm = 3000
tx_ws_ip = 0.0.0.0
tx_ws_port = 0

[tx_block]
network_id = ${net_id}
EOF
}

# ──────────────────────────────────────────────────────────────
# Setup node directory
# ──────────────────────────────────────────────────────────────
setup_node() {
  local name="$1"
  local node_dir="$DEPLOY_DIR/$name"

  local net_id="${NODE_SHARD[$name]}"
  local p2p="${NODE_P2P[$name]}"
  local http="${NODE_HTTP[$name]}"
  local is_root="${NODE_ROOT[$name]}"
  local for_explorer="${NODE_EXPLORER[$name]}"
  local keyfile="${NODE_KEYFILE[$name]}"
  local dbdir="${NODE_DBDIR[$name]}"
  local keyline="${NODE_KEYLINE[$name]}"

  log "  Setting up $name (shard=$net_id, p2p=$p2p, http=$http, explorer=$for_explorer)"

  # Read private key
  local prikey; prikey=$(get_prikey "$keyfile" "$keyline")
  [[ -n "$prikey" ]] || err "Could not read prikey for $name from $GENESIS_DIR/$keyfile line $keyline"

  # Build bootstrap
  local bootstrap; bootstrap=$(build_bootstrap)

  # Write config
  write_node_config "$node_dir" "$net_id" "$p2p" "$http" "$prikey" "$bootstrap" "$is_root" "$for_explorer"

  # Copy/link genesis DB
  if [[ -d "$GENESIS_DIR/$dbdir" ]]; then
    rm -rf "$node_dir/db"
    cp -a "$GENESIS_DIR/$dbdir" "$node_dir/db"
  else
    warn "Genesis DB not found: $GENESIS_DIR/$dbdir — node will start without genesis data"
    mkdir -p "$node_dir/db"
  fi

  # Symlink binary
  ln -sf "$BINARY" "$node_dir/shardora"

  # Symlink SSL certs
  ln -sf "$CERTS_DIR/server-cert.pem" "$node_dir/server-cert.pem"
  ln -sf "$CERTS_DIR/server-key.pem"  "$node_dir/server-key.pem"

  # Find and link GeoIP DB
  local geoip_src=""
  for p in \
    "$SOURCE_DIR/nodes_local/temp/conf/GeoLite2-City.mmdb" \
    "$SOURCE_DIR/shardoras_local/shardora/GeoLite2-City.mmdb" \
    "$SOURCE_DIR/GeoLite2-City.mmdb" \
    "/root/shardora/GeoLite2-City.mmdb"; do
    [[ -f "$p" ]] && geoip_src="$p" && break
  done
  if [[ -n "$geoip_src" ]]; then
    ln -sf "$geoip_src" "$node_dir/conf/GeoLite2-City.mmdb"
  else
    warn "GeoLite2-City.mmdb not found — node will run without geo-IP lookups"
    touch "$node_dir/conf/GeoLite2-City.mmdb"  # empty placeholder
  fi

  # Write minimal log4cpp.properties
  cat > "$node_dir/conf/log4cpp.properties" <<'LOG4CPP'
log4cpp.rootCategory=INFO, rootAppender
log4cpp.appender.rootAppender=FileAppender
log4cpp.appender.rootAppender.fileName=log/shardora.log
log4cpp.appender.rootAppender.layout=PatternLayout
log4cpp.appender.rootAppender.layout.ConversionPattern=%d [%p] %c - %m%n
LOG4CPP
}

# ──────────────────────────────────────────────────────────────
# Start a single node
# ──────────────────────────────────────────────────────────────
start_node() {
  local name="$1"
  local node_dir="$DEPLOY_DIR/$name"
  local pidfile="$node_dir/shardora.pid"

  # Don't re-start if already running
  if [[ -f "$pidfile" ]]; then
    local pid; pid=$(cat "$pidfile")
    if kill -0 "$pid" 2>/dev/null; then
      info "  $name already running (pid $pid)"
      return
    fi
    rm -f "$pidfile"
  fi

  log "  Starting $name..."
  cd "$node_dir"
  nohup ./shardora -f 0 -g 0 "$name" > log/stdout.log 2>&1 &
  echo $! > "$pidfile"
  cd - > /dev/null
}

# ──────────────────────────────────────────────────────────────
# Nginx configuration
# ──────────────────────────────────────────────────────────────
configure_nginx() {
  section "Nginx"

  command -v nginx >/dev/null 2>&1 || {
    log "Installing nginx..."
    apt-get update -qq && apt-get install -y -qq nginx
  }

  mkdir -p "$WEB_ROOT"

  # Write nginx site config
  cat > /etc/nginx/sites-available/chainbaas <<NGINX
# Chainbaas blockchain explorer

# Serve Vue frontend
server {
    listen 80;
    server_name _;
    root $WEB_ROOT;
    index index.html;

    # SPA: send all unknown paths to index.html
    location / {
        try_files \$uri \$uri/ /index.html;
    }

    # Explorer API shard proxy (HTTP → HTTPS with self-signed cert)
    # Root shard
    location /shard/root/ {
        rewrite ^/shard/root/(.*)\$ /\$1 break;
        proxy_pass https://127.0.0.1:30302;
        proxy_ssl_verify off;
        proxy_set_header Host \$host;
    }
}

# HTTP proxies for each shard explorer endpoint
# Browser can access http://SERVER_IP:4030X/explorer/* without cert issues
server {
    listen 40302;
    server_name _;
    location / {
        proxy_pass https://127.0.0.1:30302;
        proxy_ssl_verify off;
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Access-Control-Allow-Headers "Content-Type" always;
        if (\$request_method = OPTIONS) { return 204; }
    }
}

server {
    listen 40303;
    server_name _;
    location / {
        proxy_pass https://127.0.0.1:30303;
        proxy_ssl_verify off;
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Access-Control-Allow-Headers "Content-Type" always;
        if (\$request_method = OPTIONS) { return 204; }
    }
}

server {
    listen 40304;
    server_name _;
    location / {
        proxy_pass https://127.0.0.1:30304;
        proxy_ssl_verify off;
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Access-Control-Allow-Headers "Content-Type" always;
        if (\$request_method = OPTIONS) { return 204; }
    }
}

server {
    listen 40305;
    server_name _;
    location / {
        proxy_pass https://127.0.0.1:30305;
        proxy_ssl_verify off;
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Access-Control-Allow-Headers "Content-Type" always;
        if (\$request_method = OPTIONS) { return 204; }
    }
}

server {
    listen 40306;
    server_name _;
    location / {
        proxy_pass https://127.0.0.1:30306;
        proxy_ssl_verify off;
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Access-Control-Allow-Headers "Content-Type" always;
        if (\$request_method = OPTIONS) { return 204; }
    }
}
NGINX

  # Enable site
  ln -sf /etc/nginx/sites-available/chainbaas /etc/nginx/sites-enabled/chainbaas
  rm -f /etc/nginx/sites-enabled/default 2>/dev/null || true

  nginx -t && log "Nginx config valid"
  systemctl reload nginx || systemctl restart nginx
  log "Nginx running."
}

# ──────────────────────────────────────────────────────────────
# Build and deploy frontend
# ──────────────────────────────────────────────────────────────
build_frontend() {
  section "Frontend Build"

  # Ensure Node.js is available
  if ! command -v node >/dev/null 2>&1; then
    log "Node.js not found — installing via NodeSource..."
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
  fi

  log "Node.js: $(node --version)  npm: $(npm --version)"

  # Write server-specific explorer-config.js
  cat > "$CHAINBAAS_DIR/public/explorer-config.js" <<JS
// Auto-generated by deploy_5shard_explorer.sh for $SERVER_IP
window.__EXPLORER_SHARDS__ = [
  { id: 1, networkId: 2, name: 'Root Shard', url: 'http://$SERVER_IP:40302' },
  { id: 2, networkId: 3, name: 'Shard 3',    url: 'http://$SERVER_IP:40303' },
  { id: 3, networkId: 4, name: 'Shard 4',    url: 'http://$SERVER_IP:40304' },
  { id: 4, networkId: 5, name: 'Shard 5',    url: 'http://$SERVER_IP:40305' },
  { id: 5, networkId: 6, name: 'Shard 6',    url: 'http://$SERVER_IP:40306' },
]
JS
  log "Wrote explorer-config.js with 5 shard endpoints"

  cd "$CHAINBAAS_DIR"
  log "Installing npm dependencies..."
  npm install --prefer-offline 2>&1 | tail -5
  log "Building Vue app..."
  npm run build 2>&1 | tail -10
  cd - > /dev/null

  local dist_dir="$CHAINBAAS_DIR/dist"
  [[ -d "$dist_dir" ]] || err "Build failed: dist/ not found"

  mkdir -p "$WEB_ROOT"
  cp -a "$dist_dir/." "$WEB_ROOT/"
  log "Frontend deployed to $WEB_ROOT"
}

# ──────────────────────────────────────────────────────────────
# Open firewall ports
# ──────────────────────────────────────────────────────────────
open_ports() {
  section "Firewall"
  if command -v ufw >/dev/null 2>&1; then
    # Web + explorer proxy ports
    for port in 80 40302 40303 40304 40305 40306; do
      ufw allow "$port/tcp" >/dev/null 2>&1 && log "  ufw: allowed $port/tcp" || true
    done
    # P2P ports
    for port in 12001 12002 13001 13002 14001 14002 15001 15002 16001 16002; do
      ufw allow "$port/udp" >/dev/null 2>&1 || true
      ufw allow "$port/tcp" >/dev/null 2>&1 || true
    done
    log "Firewall rules updated."
  else
    warn "ufw not found — make sure ports 80,40302-40306 are reachable from outside."
  fi
}

# ──────────────────────────────────────────────────────────────
# Write systemd service units for autostart
# ──────────────────────────────────────────────────────────────
write_systemd_units() {
  section "Systemd"
  for name in "${ALL_NODES[@]}"; do
    local node_dir="$DEPLOY_DIR/$name"
    cat > "/etc/systemd/system/shardora-${name}.service" <<SVC
[Unit]
Description=Shardora node ${name}
After=network.target
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
Type=simple
WorkingDirectory=${node_dir}
ExecStart=${node_dir}/shardora -f 0 -g 0 ${name}
Restart=on-failure
RestartSec=5s
StandardOutput=append:${node_dir}/log/stdout.log
StandardError=append:${node_dir}/log/stderr.log

[Install]
WantedBy=multi-user.target
SVC
    systemctl daemon-reload
    systemctl enable "shardora-${name}" 2>/dev/null || true
    log "  Created systemd unit: shardora-${name}"
  done
}

# ──────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────
main() {
  setup_layout

  if [[ "$DO_STATUS" == "1" ]]; then
    node_status
    exit 0
  fi

  if [[ "$DO_STOP" == "1" ]]; then
    stop_nodes
    exit 0
  fi

  if [[ "$DO_RESTART" == "1" ]]; then
    stop_nodes
    sleep 2
  fi

  section "Deploy 5-Shard Shardora Explorer"
  info "  Source:     $SOURCE_DIR"
  info "  Deploy dir: $DEPLOY_DIR"
  info "  Server IP:  $SERVER_IP"
  info "  Shards:     root(2) + 3,4,5,6  |  $NODE_COUNT nodes/shard = 10 total"
  echo ""

  # 1. Build
  if [[ "$SKIP_BUILD" == "0" ]]; then
    build_shardora
  else
    [[ -x "$BINARY" ]] || err "Binary not found: $BINARY (remove --skip-build to compile)"
    log "Skipping build (using existing binary)"
  fi

  # 2. Certs
  gen_certs

  # 3. Genesis
  if [[ "$SKIP_GENESIS" == "0" ]]; then
    run_genesis
  else
    [[ -f "$GENESIS_DIR/root_nodes" ]] || err "No genesis files at $GENESIS_DIR (remove --skip-genesis)"
    log "Skipping genesis (using existing key files)"
  fi

  # 4. Setup node directories
  section "Node Setup"
  mkdir -p "$DEPLOY_DIR"
  for name in "${ALL_NODES[@]}"; do
    setup_node "$name"
  done

  # 5. Frontend
  if [[ "$SKIP_FRONTEND" == "0" ]]; then
    build_frontend
  else
    log "Skipping frontend build"
  fi

  # 6. Nginx
  configure_nginx

  # 7. Firewall
  open_ports

  # 8. Systemd units
  if [[ "$(id -u)" == "0" ]]; then
    write_systemd_units
  else
    warn "Not running as root — skipping systemd unit creation"
  fi

  # 9. Start nodes
  section "Starting Nodes"
  # Start root nodes first, wait briefly, then consensus nodes
  for name in r1 r2; do
    start_node "$name"
  done
  log "Waiting 5s for root shard to initialise..."
  sleep 5
  for name in s3_1 s3_2 s4_1 s4_2 s5_1 s5_2 s6_1 s6_2; do
    start_node "$name"
    sleep 1
  done

  sleep 3
  node_status

  section "Done"
  echo ""
  echo -e "${GREEN}Explorer URLs:${NC}"
  echo "  Frontend:   http://$SERVER_IP/"
  echo "  Root Shard: http://$SERVER_IP:40302/explorer/chain-info"
  echo "  Shard 3:    http://$SERVER_IP:40303/explorer/chain-info"
  echo "  Shard 4:    http://$SERVER_IP:40304/explorer/chain-info"
  echo "  Shard 5:    http://$SERVER_IP:40305/explorer/chain-info"
  echo "  Shard 6:    http://$SERVER_IP:40306/explorer/chain-info"
  echo ""
  echo -e "${CYAN}Logs:${NC}"
  for name in "${ALL_NODES[@]}"; do
    echo "  tail -f $DEPLOY_DIR/$name/log/shardora.log"
  done
  echo ""
  echo -e "${CYAN}Management:${NC}"
  echo "  $0 --status            # check node status"
  echo "  $0 --restart           # stop all and restart"
  echo "  $0 --stop              # stop all nodes"
  echo "  $0 --skip-build --skip-genesis --skip-frontend --restart  # fast restart"
}

main "$@"
