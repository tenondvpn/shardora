#!/usr/bin/env python3
"""
Deploy chainbaas to remote server via SSH/SFTP (paramiko).
Target: 47.111.109.8  root  Xf4aGbTaf&
"""
import os, sys, tarfile, tempfile, time, paramiko

HOST     = "47.111.109.8"
USER     = "root"
PASSWORD = "Xf4aGbTaf&"
PORT     = 22
REMOTE   = "/root/chainbaas"

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
CHAINBAAS    = os.path.join(SCRIPT_DIR, "chainbaas")
NGINX_CONF   = os.path.join(CHAINBAAS, "nginx_chainbaas.conf")

FILES = [
    ("dist",              "dist"),
    ("explorer_proxy.py", "explorer_proxy.py"),
    ("solc_server.py",    "solc_server.py"),
    ("nginx_chainbaas.conf", "nginx_chainbaas.conf"),
]

def log(msg): print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)

# ── build tar ─────────────────────────────────────────────────
log("打包部署文件...")
tmp = tempfile.NamedTemporaryFile(suffix=".tar.gz", delete=False)
tmp.close()
with tarfile.open(tmp.name, "w:gz") as tar:
    for local_rel, arc_name in FILES:
        local_path = os.path.join(CHAINBAAS, local_rel)
        tar.add(local_path, arcname=arc_name)
size_kb = os.path.getsize(tmp.name) // 1024
log(f"  打包完成: {tmp.name} ({size_kb} KB)")

# ── connect ───────────────────────────────────────────────────
log(f"连接 {HOST}:{PORT} ...")
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=20)
log("  SSH 已连接")

sftp = ssh.open_sftp()

def run(cmd, check=True):
    _, stdout, stderr = ssh.exec_command(cmd, timeout=60)
    out = stdout.read().decode().strip()
    err = stderr.read().decode().strip()
    if out: print("  " + out)
    if err: print("  STDERR: " + err)
    return out

# ── upload ────────────────────────────────────────────────────
remote_tar = "/tmp/chainbaas_deploy.tar.gz"
log(f"上传 {size_kb} KB ...")

def progress(sent, total):
    pct = sent * 100 // total
    print(f"\r  上传进度: {pct}%", end="", flush=True)

sftp.put(tmp.name, remote_tar, callback=progress)
print()
log("  上传完成")

# ── extract ───────────────────────────────────────────────────
log("解包到 /root/chainbaas ...")
run(f"mkdir -p {REMOTE} && tar -xzf {remote_tar} -C {REMOTE}/ && rm -f {remote_tar}")

# ── nginx ─────────────────────────────────────────────────────
log("配置 nginx ...")
run("command -v nginx >/dev/null 2>&1 || (apt-get update -qq && apt-get install -y -qq nginx)")
run(f"cp {REMOTE}/nginx_chainbaas.conf /etc/nginx/conf.d/chainbaas.conf")
run("rm -f /etc/nginx/sites-enabled/default 2>/dev/null; nginx -t")
run("systemctl is-active nginx >/dev/null 2>&1 && systemctl reload nginx || systemctl restart nginx")
log("  nginx 已重载")

# ── python services ───────────────────────────────────────────
log("重启后端 Python 服务 ...")
run("pkill -f 'python3.*solc_server.py' 2>/dev/null; pkill -f 'python3.*explorer_proxy.py' 2>/dev/null; sleep 1; true")
run(f"nohup python3 {REMOTE}/solc_server.py > {REMOTE}/solc_server.log 2>&1 &")
run(f"nohup python3 {REMOTE}/explorer_proxy.py > {REMOTE}/explorer_proxy.log 2>&1 &")
time.sleep(1)

# ── verify ────────────────────────────────────────────────────
log("验证部署结果 ...")
nginx_status = run("systemctl is-active nginx 2>/dev/null || echo inactive")
dist_ok      = run(f"[ -f {REMOTE}/dist/index.html ] && echo ok || echo missing")
log(f"  nginx={nginx_status}  dist={dist_ok}")

sftp.close()
ssh.close()
os.unlink(tmp.name)

log("✓ 部署完成！访问: http://47.111.109.8:8080")
