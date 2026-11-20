#!/usr/bin/env python3
"""Read-only reconnaissance of the jump host 47.111.109.8."""
import paramiko

HOST, USER, PASS = '47.111.109.8', 'root', 'Xf4aGbTaf&'

CMDS = [
    ('uname',            'hostname; uname -a'),
    ('disk',             'df -h / /root'),
    ('root dirs',        'ls -la /root'),
    ('shardora trees',   'ls -d /root/shardora* /root/shardoras 2>/dev/null'),
    ('chainbaas dist',   'ls -la /root/chainbaas/dist/ /root/chainbaas/dist/assets/ 2>/dev/null | head -30'),
    ('nginx sites',      'ls -la /etc/nginx/conf.d/ /etc/nginx/sites-enabled/ 2>/dev/null'),
    ('listen 8080',      "grep -rn 'listen' /etc/nginx/ 2>/dev/null | grep -v '#' | head -20"),
    ('server_name',      "grep -rn 'server_name' /etc/nginx/ 2>/dev/null | grep -v '#' | head -20"),
    ('served index',     "curl -s --max-time 8 http://127.0.0.1:8080/ | head -20"),
    ('served assets dir','curl -s -o /dev/null -w "%{http_code}" --max-time 8 http://127.0.0.1:8080/assets/index-Cc-OuJIL.js; echo " <- new bundle HTTP code"'),
    ('ssh to nodes',     'for ip in 192.168.26.203 192.168.26.213 192.168.26.211 192.168.26.204 192.168.26.215; do '
                         'timeout 4 bash -c "echo > /dev/tcp/$ip/22" 2>/dev/null && echo "$ip:22 OPEN" || echo "$ip:22 closed"; done'),
    ('node http ports',  'for p in 22001 23001 24001 25001 26001; do '
                         'timeout 4 bash -c "echo > /dev/tcp/192.168.26.203/$p" 2>/dev/null && echo "203:$p OPEN" || echo "203:$p closed"; done'),
]

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, username=USER, password=PASS, timeout=20)
for label, cmd in CMDS:
    _, out, err = ssh.exec_command(cmd, timeout=180)
    body = (out.read() + err.read()).decode(errors='replace').strip()
    print(f'\n===== {label} =====')
    print(body if body else '(empty)')
ssh.close()
