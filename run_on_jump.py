#!/usr/bin/env python3
"""Run a shell command on the jump host.

Usage:
    python run_on_jump.py -f <command_file> [timeout_sec]
    python run_on_jump.py "<command>" [timeout_sec]

Prefer -f: PowerShell mangles `$`, quotes and backslashes in inline arguments,
and a command containing 'diffstat' after whitespace gets mistaken for argv[2].
"""
import sys
import paramiko

HOST, USER, PASS = '47.111.109.8', 'root', 'Xf4aGbTaf&'

args = sys.argv[1:]
if not args:
    print(__doc__)
    sys.exit(1)

if args[0] == '-f':
    with open(args[1], 'r', encoding='utf-8') as fh:
        cmd = fh.read()
    timeout = int(args[2]) if len(args) > 2 else 300
else:
    cmd = args[0]
    timeout = int(args[1]) if len(args) > 1 else 300

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, username=USER, password=PASS, timeout=20)
_, out, err = ssh.exec_command(cmd, timeout=timeout)
o = out.read().decode(errors='replace')
e = err.read().decode(errors='replace')
print(o)
if e.strip():
    print('--- stderr ---')
    print(e)
ssh.close()
