#!/usr/bin/env python3
"""Inspect the shardora source tree + build state on the jump host."""
import paramiko

HOST, USER, PASS = '47.111.109.8', 'root', 'Xf4aGbTaf&'

CMDS = [
    ('git status',   'cd /root/shardora && git log --oneline -3 2>&1; echo ---; git status --short 2>&1 | head -20'),
    ('search route present?', "cd /root/shardora && grep -rn 'explorer/search' src/ 2>/dev/null | head"),
    ('SearchAddresses present?', "cd /root/shardora && grep -rn 'SearchAddresses' src/ 2>/dev/null | head"),
    ('build dirs',   'cd /root/shardora && ls -d cbuild* build 2>/dev/null; echo ---; ls -la cbuild_Release/shardora 2>/dev/null'),
    ('build log tail', 'tail -20 /root/build_shardora.log'),
    ('deploy dir',   'ls -la /root/deploy | head -30'),
    ('tools dir',    'find /root/tools -maxdepth 2 -type f 2>/dev/null | head -20'),
    ('running nodes','ps aux | grep -c "[s]hardora"; echo ---; ps aux | grep "[s]hardora" | head -5 | cut -c1-160'),
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
