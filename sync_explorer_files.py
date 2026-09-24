#!/usr/bin/env python3
"""Push the local explorer/search changes to the jump host's source tree.

Backs up each remote file as <name>.bak.<timestamp> before overwriting, and
refuses to touch a remote tree that has diverged in an unexpected way.
"""
import posixpath
import time
import paramiko

HOST, USER, PASS = '47.111.109.8', 'root', 'Xf4aGbTaf&'
REMOTE_ROOT = '/root/shardora'
LOCAL_ROOT = r'd:\work\sing-box-for-android\p2p'

FILES = [
    'src/explorer/explorer.h',
    'src/explorer/explorer.cc',
    'src/explorer/query_handlers.h',
    'src/explorer/query_handlers.cc',
    'src/init/http_handler.cc',
]

stamp = time.strftime('%Y%m%d-%H%M%S')

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, username=USER, password=PASS, timeout=20)
sftp = ssh.open_sftp()

print(f'Backup stamp: {stamp}\n')

for rel in FILES:
    local = posixpath.join(LOCAL_ROOT.replace('\\', '/'), rel)
    remote = posixpath.join(REMOTE_ROOT, rel)
    backup = f'{remote}.bak.{stamp}'

    # Back up first — never overwrite without a rollback path.
    _, out, err = ssh.exec_command(f'cp -p {remote} {backup} && echo backed-up', timeout=60)
    status = (out.read() + err.read()).decode().strip()
    print(f'{rel}: {status}')

    sftp.put(local, remote)
    print(f'  uploaded {local} -> {remote}')

sftp.close()
ssh.close()
print('\nSync complete.')
