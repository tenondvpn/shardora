import paramiko
from pathlib import Path

HOST = '47.111.109.8'
USER = 'root'
PASS = 'Xf4aGbTaf&'

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(HOST, port=22, username=USER, password=PASS, timeout=15)

# Check current nginx config
_, stdout, _ = ssh.exec_command('cat /root/chainbaas/nginx_chainbaas.conf')
print("=== existing nginx config ===")
print(stdout.read().decode())

_, stdout, _ = ssh.exec_command('ls /etc/nginx/conf.d/ && cat /etc/nginx/conf.d/chainbaas.conf 2>/dev/null')
print("=== /etc/nginx/conf.d/ ===")
print(stdout.read().decode())

_, stdout, _ = ssh.exec_command('curl -s http://127.0.0.1:8080/ | head -5')
print("=== nginx port 8080 test ===")
print(stdout.read().decode())

ssh.close()
