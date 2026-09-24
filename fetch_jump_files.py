#!/usr/bin/env python3
"""Download files from the jump host to d:\\tmp for local inspection."""
import sys
import posixpath
import paramiko

HOST, USER, PASS = '47.111.109.8', 'root', 'Xf4aGbTaf&'
DEST = r'd:\tmp\jump'

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, username=USER, password=PASS, timeout=20)
sftp = ssh.open_sftp()

for remote in sys.argv[1:]:
    local = posixpath.join(DEST, remote.strip('/').replace('/', '_'))
    try:
        sftp.get(remote, local)
        print(f'{remote} -> {local}')
    except Exception as exc:
        print(f'{remote}: FAILED {exc}')

sftp.close()
ssh.close()
