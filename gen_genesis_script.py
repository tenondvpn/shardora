import argparse
from eth_keys import keys, datatypes
import sha3
from secp256k1 import PrivateKey, PublicKey
from eth_utils import decode_hex, encode_hex
from ecdsa import SigningKey, SECP256k1

import os
import yaml
import toml
import re

node_sk_map = {}

def input2sk(input: str) -> str:
    sk_str = node_sk_map.get(input)
    if sk_str is None:
        sk_str = random_sk()
        node_sk_map[input] = sk_str
    return sk_str

def gen_node_sk(node_name: str) -> str:
    return input2sk(node_name)

def gen_account_sks(net_id: int, num: int) -> list[str]:
    return [input2sk(f'{net_id}_{i}') for i in range(num)]

def sk2account(sk: str) -> str:
    _, pk_bytes, _ = gen_keypair(sk)
    return pubkey_to_account(pk_bytes)

def gen_keypair(private_key_str: str) -> tuple[bytes, bytes]:
    privkey = bytes(bytearray.fromhex(private_key_str))
    sk = SigningKey.from_string(privkey, curve=SECP256k1)
    pk = sk.verifying_key
    public_key = pk.to_string()
    compressed_public_key = pk.to_string("compressed")
    # pk = keys.PrivateKey(privkey)
    return privkey, public_key, compressed_public_key

def get_compressed_pk_str(sk_str: str) -> str:
    _, _, comp_pk_bytes = gen_keypair(sk_str)
    return comp_pk_bytes.hex()

def pubkey_to_account(pub_key_bytes: bytes) -> str:
    addr = keccak256_bytes(pub_key_bytes)
    return addr[len(addr)-40:len(addr)]

def keccak256_bytes(b: bytes) -> str:
    k = sha3.keccak_256()
    k.update(b)
    return k.hexdigest()

def keccak256(s: str) -> str:
    k = sha3.keccak_256()
    k.update(bytes(s, 'utf-8'))
    return k.hexdigest()

def random_sk():
    # 生成 32 字节的随机数作为私钥
    sk = SigningKey.generate(curve=SECP256k1)
        
    return sk.to_string().hex()

def parse_server_yml_file(file_path: str):
    with open(file_path) as f:
        data = yaml.safe_load(f)
    return data

def _get_node_sks_from_server_conf(server_conf, net_id):
    node_names = [n['name'] for n in server_conf['nodes'] if n['net'] == net_id]
    return [gen_node_sk(n) for n in node_names]

def _gen_accounts_with_server_conf(server_conf, net_id):
    account_sks_from_server_conf = server_conf['account_sks'].get(net_id, [])
    num = 256 - len(account_sks_from_server_conf)
    random_sks = gen_account_sks(net_id, num)
    accounts = [sk2account(sk) for sk in account_sks_from_server_conf + random_sks]
    return accounts
 

def gen_genesis_yaml_file(server_conf: dict, file_path: str):
    root = {
        'net_id': 2,
        'sks': _get_node_sks_from_server_conf(server_conf, 2),
        'accounts': _gen_accounts_with_server_conf(server_conf, 2),
    }
    shards = []
    net_ids = list(set([node['net'] for node in server_conf['nodes']]))
    shard_ids = [net_id for net_id in net_ids if net_id != 2]
    for shard_id in shard_ids:
        shards.append({
            'net_id': shard_id,
            'sks': _get_node_sks_from_server_conf(server_conf, shard_id),
            'accounts': _gen_accounts_with_server_conf(server_conf, shard_id),
        })
    genesis_conf = {
        'root': root,
        'shards': shards,
    }
    with open(file_path, 'w') as f:
        yaml.dump(genesis_conf, f)

def _get_bootstrap_str(node_name, server_conf: dict) -> str:
    for node in server_conf['nodes']:
        if node['name'] == node_name:
            comp_pk_str = get_compressed_pk_str(gen_node_sk(node_name))
            return f'{comp_pk_str}:{node["server"]}:{node["tcp_port"]}'

def gen_zjnodes(server_conf: dict, zjnodes_folder):
    if zjnodes_folder.endswith('/'):
        zjnodes_folder = zjnodes_folder[:-1]
    
    root_boostrap_strs = [_get_bootstrap_str(node['name'], server_conf) for node in server_conf['nodes'] if node['net'] == 2]
    root_boostrap_str = ','.join(root_boostrap_strs)

    for node in server_conf['nodes']:
        sk = gen_node_sk(node['name'])
        zjchain_conf = {
            'db': {
                'path': './db',
            },
            'log': {
                'path': 'log/zjchain.log',
            },
            'zjchain': {
                'bootstrap': root_boostrap_str,
                'ck_ip': '127.0.0.1',
                'ck_passworkd': '',
                'ck_user': 'default',
                'country': 'NL',
                'first_node': 1 if node['name'] == 'r1' else 0,
                'http_port': node['http_port'],
                'local_ip': node['server'],
                'local_port': node['tcp_port'],
                'net_id': node['net'],
                'prikey': sk,
                'show_cmd': 0,
                'for_ck': False
            },
            'tx_block': {
                'network_id': node['net']
            }
        }

        sub_folder = f'{zjnodes_folder}/{node["name"]}'
        sub_conf_folder = f'{sub_folder}/conf'
        if not os.path.exists(sub_folder):
            os.makedirs(sub_folder)
            if not os.path.exists(sub_conf_folder):
                os.makedirs(sub_conf_folder)

        with open(f'{sub_conf_folder}/zjchain.conf', 'w') as f:
            toml.dump(zjchain_conf, f)

def gen_genesis_sh_file(server_conf: dict, file_path):
    net_names_map = {}
    all_names = []
    for node in server_conf['nodes']:
        all_names.append('"' + node['name'] + '"')
        if net_names_map.get(node['net']) is None:
            net_names_map[node['net']] = []
        net_names_map[node['net']].append('"' + node['name'] + '"')

    all_names_str = ' '.join(all_names)
    net_names_str_map = {}
    for k, v in net_names_map.items():
        net_names_str = ' '.join(v)
        net_names_str_map[k] = net_names_str

    net_ids = sorted(list(set([node['net'] for node in server_conf['nodes']])))

    code_str = """
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

sh build.sh a $TARGET
sudo rm -rf /root/zjnodes
sudo cp -rf ./zjnodes /root
sudo cp -rf ./deploy /root

rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

"""

    net_keys = []
    for net_id in net_ids:
        key = 'root' if net_id == 2 else 'shard' + str(net_id)
        net_keys.append(key)
        value = net_names_str_map[net_id]
        code_str += f"{key}=({value})\n"
    code_str += f"nodes=({all_names_str})\n"

    code_str += """
for node in "${nodes[@]}"; do
    mkdir -p "/root/zjnodes/${node}/log"
    cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${node}/conf
    cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${node}/conf
done
mkdir -p /root/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml

for node in "${nodes[@]}"; do
    sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/${node}
done
sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain

"""

    for net_id in net_ids:
        arg_str = '-U' if net_id == 2 else '-S ' + str(net_id)
        code_str += f"cd /root/zjnodes/zjchain && ./zjchain {arg_str}\n"

    code_str += "\n"

    for net_id in net_ids:
        net_key = 'root' if net_id == 2 else 'shard' + str(net_id)
        db_str = 'root_db' if net_id == 2 else 'shard_db_' + str(net_id)
        code_str += f"""
for node in "${{{net_key}[@]}}"; do
	cp -rf /root/zjnodes/zjchain/{db_str} /root/zjnodes/${{node}}/db
done

"""

    code_str += """
clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
"""

    with open(file_path, 'w') as f:
        f.write(code_str)


def gen_run_nodes_sh_file(server_conf: dict, file_path, build_genesis_path, tag):
    code_str = """
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
"""

    server_node_map = {}
    server0 = ''
    secondary_servers = set()
    for node in server_conf['nodes']:
        if node['name'] == 'r1':
            server0 = node['server']
        
        secondary_servers.add(node['server'])
        
        if server_node_map.get(node['server']) is None:
            server_node_map[node['server']] = []
        server_node_map[node['server']].append(node['name'])

    secondary_servers.remove(server0)
    server_name_map = {
        'server0': server0,
    }
    for i, server in enumerate(secondary_servers):
        server_name_map[f"server{i+1}"] = server
    
    for server_name, server_ip in server_name_map.items():
        code_str += f"{server_name}={server_ip}\n"

    code_str += f"target=$1\n"

    server0_node_names_str = ' '.join(server_node_map[server0])
    server0_pass = server_conf['passwords'].get(server0, '')
    code_str += f"""
echo "[$server0]"
# sshpass -p {server0_pass} ssh -o StrictHostKeyChecking=no root@$server0 <<EOF
cd /root/xufei/zjchain && sh {build_genesis_path} $target
cd /root && sh -x fetch.sh 127.0.0.1 ${{server0}} $pass {server0_node_names_str}
# EOF

"""
    
    for server_name, server_ip in server_name_map.items():
        if server_name == 'server0':
            continue
        server_node_names_str = ' '.join(server_node_map[server_ip])
        server_pass = server_conf['passwords'].get(server_ip, '')
        code_str += f"""
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -o StrictHostKeyChecking=no root@${server_name} <<EOF
rm -rf /root/zjnodes;
sshpass -p '{server0_pass}' scp -o StrictHostKeyChecking=no root@"${{server0}}":/root/fetch.sh /root/
cd /root && sh -x fetch.sh ${{server0}} ${{{server_name}}} '{server0_pass}' {server_node_names_str}
EOF

"""
        
    # code_str += "wait\n"
        
    code_str += """
echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep zjchain | grep {tag} | awk -F' ' '{{print $2}}' | xargs kill -9
"""

    for server_name, server_ip in server_name_map.items():
        if server_name == 'server0':
            continue
        server_pass = server_conf['passwords'].get(server_ip, '')
        code_str += f"""
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -o StrictHostKeyChecking=no root@${server_name} <<"EOF"
ps -ef | grep zjchain | grep {tag} | awk -F' ' '{{print $2}}' | xargs kill -9
EOF
"""
        
    code_str += """
echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="
"""

    code_str += f"""
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 {tag}> /dev/null 2>&1 &

sleep 3
"""
    
    for server_name, server_ip in server_name_map.items():
        if server_name == 'server0':
            server0_nodes = server_node_map[server_ip]
            server0_nodes.remove('r1')
            server_nodes_str = ' '.join(server0_nodes)
            server_pass = server_conf['passwords'].get(server_ip, '')
            code_str += f"""
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64;
for node in {server_nodes_str}; do
    cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node {tag}> /dev/null 2>&1 &
done

"""      
        else:
            server_nodes_str = ' '.join(server_node_map[server_ip])
        
            server_pass = server_conf['passwords'].get(server_ip, '')
            code_str += f"""
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -o StrictHostKeyChecking=no -f root@${server_name} bash -c "'\\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \\
for node in {server_nodes_str}; do \\
    cd /root/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node {tag}> /dev/null 2>&1 &\\
done \\
'"

    """      

    code_str += """
echo "==== STEP3: DONE ===="
"""

    with open(file_path, 'w') as f:
        f.write(code_str)

def modify_shard_num_in_src_code(server_conf, file_path='./src/network/network_utils.h'):
    shards_set = set()
    for node in server_conf['nodes']:
        shards_set.add(int(node['net']))
    shards_set.remove(2)

    with open(file_path, 'r') as f:
        content = f.read()
    
    new_content = re.sub(r'static const uint32_t kConsensusShardEndNetworkId = \d+u;', f"static const uint32_t kConsensusShardEndNetworkId = {3+len(shards_set)}u;", content)
    with open(file_path, 'w') as f:
        f.write(new_content)



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', help='nodes_conf.yml 文件位置', default='')
    parser.add_argument('--tag', help='tag', default='')
    args = parser.parse_args()
    if args.config == '':
        args.config = './nodes_conf.yml'

    file_path = args.config
    server_conf = parse_server_yml_file(file_path)
    build_genesis_path = './build_genesis.sh'
    gen_zjnodes(server_conf, "./zjnodes")
    gen_genesis_yaml_file(server_conf, "./conf/genesis.yml")
    gen_genesis_sh_file(server_conf, build_genesis_path)
    gen_run_nodes_sh_file(server_conf, "./deploy_genesis_multi_server.sh", build_genesis_path, tag=args.tag)
    modify_shard_num_in_src_code(server_conf)

if __name__ == '__main__':
    main()