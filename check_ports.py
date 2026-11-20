import paramiko

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('47.111.109.8', port=22, username='root', password='Xf4aGbTaf&', timeout=15)
print('Connected to 47.111.109.8')

nodes = {
    'shard2': '192.168.26.203',
    'shard3': '192.168.26.213',
    'shard4': '192.168.26.211',
    'shard5': '192.168.26.204',
    'shard6': '192.168.26.215',
}

for shard, ip in nodes.items():
    for port in [22001, 23001, 24001, 25001, 26001, 18080, 8080, 30302]:
        cmd = f'timeout 2 bash -c "echo > /dev/tcp/{ip}/{port}" 2>/dev/null && echo "{shard}:{ip}:{port}=OPEN" || echo "{shard}:{ip}:{port}=CLOSED"'
        _, stdout, _ = ssh.exec_command(cmd)
        out = stdout.read().decode().strip()
        if 'OPEN' in out:
            print(out)

_, stdout, _ = ssh.exec_command('ls /root/chainbaas/ 2>/dev/null || echo "no chainbaas dir"')
print("chainbaas dir:", stdout.read().decode().strip())

_, stdout, _ = ssh.exec_command('nginx -v 2>&1 || echo "no nginx"')
print("nginx:", stdout.read().decode().strip())

ssh.close()
