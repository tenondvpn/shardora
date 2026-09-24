#!/usr/bin/env python3
import paramiko
from pathlib import Path

HOST = '47.111.109.8'
PORT = 22
USER = 'root'
PASS = 'Xf4aGbTaf&'
LOCAL_DIR = Path(r'd:\work\sing-box-for-android\p2p\chainbaas\dist')
REMOTE_DIR = '/root/chainbaas/dist'

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, port=PORT, username=USER, password=PASS, timeout=15)
print(f'Connected to {HOST}')

sftp = ssh.open_sftp()

def ensure_remote_dir(path):
    try:
        sftp.stat(path)
    except FileNotFoundError:
        sftp.mkdir(path)

def upload_dir(local_path: Path, remote_path: str):
    ensure_remote_dir(remote_path)
    for item in local_path.iterdir():
        remote_item = f'{remote_path}/{item.name}'
        if item.is_dir():
            upload_dir(item, remote_item)
        else:
            print(f'  {item.relative_to(LOCAL_DIR)}')
            sftp.put(str(item), remote_item)

print(f'Uploading {LOCAL_DIR} -> {REMOTE_DIR}')
upload_dir(LOCAL_DIR, REMOTE_DIR)
sftp.close()

# Reload nginx
_, stdout, stderr = ssh.exec_command('nginx -t 2>&1 && nginx -s reload 2>&1')
out = stdout.read().decode() + stderr.read().decode()
print('nginx reload:', out.strip())

ssh.close()
print('Done.')
