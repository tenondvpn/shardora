#!/usr/bin/env python3
"""Deploy the chainbaas frontend to the jump host, pruning stale hashed assets.

The dist/assets directory accumulates one garbage bundle per deploy because
Vite emits content-hashed filenames. This removes any asset the fresh
index.html no longer references, then uploads and reloads nginx.
"""
import os
import re
from pathlib import Path
import paramiko

HOST, PORT, USER, PASS = '47.111.109.8', 22, 'root', 'Xf4aGbTaf&'
LOCAL_DIR = r'd:\work\sing-box-for-android\p2p\chainbaas\dist'
REMOTE_DIR = '/root/chainbaas/dist'

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, port=PORT, username=USER, password=PASS, timeout=20)
sftp = ssh.open_sftp()
print(f'Connected to {HOST}')


def ensure_dir(path):
    try:
        sftp.stat(path)
    except FileNotFoundError:
        sftp.mkdir(path)


def upload_dir(local, remote):
    ensure_dir(remote)
    for item in sorted(Path(local).iterdir()):
        r = f'{remote}/{item.name}'
        if item.is_dir():
            upload_dir(item, r)
        else:
            sftp.put(str(item), r)
            print(f'  {item.name}')


# Which asset filenames does the new index.html actually reference?
with open(os.path.join(LOCAL_DIR, 'index.html'), encoding='utf-8') as fh:
    html = fh.read()
keep = set(re.findall(r'assets/([A-Za-z0-9._-]+)', html))
print(f'index.html references: {sorted(keep)}')

# Prune stale bundles so the directory does not grow one file per deploy.
asset_dir = f'{REMOTE_DIR}/assets'
try:
    stale = [f for f in sftp.listdir(asset_dir) if f not in keep]
except FileNotFoundError:
    stale = []
for name in stale:
    sftp.remove(f'{asset_dir}/{name}')
    print(f'  pruned stale {name}')

print(f'Uploading {LOCAL_DIR} -> {REMOTE_DIR}')
upload_dir(LOCAL_DIR, REMOTE_DIR)
sftp.close()

_, stdout, stderr = ssh.exec_command('nginx -t 2>&1 && nginx -s reload 2>&1')
print('nginx:', (stdout.read() + stderr.read()).decode().strip())

_, stdout, _ = ssh.exec_command(
    'echo "--- served ---"; grep -o "assets/index-[A-Za-z0-9_-]*\\.js" '
    f'{REMOTE_DIR}/index.html; ls {asset_dir}'
)
print(stdout.read().decode().strip())
ssh.close()
print('Done.')
