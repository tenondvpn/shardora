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

def gen_node_sk(node_name: str, server_conf: dict) -> str:
    sk = server_conf.get('node_sks', {}).get(node_name, '')
    if sk != '':
        return sk
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
    return [gen_node_sk(n, server_conf) for n in node_names]

def _gen_accounts_with_server_conf(server_conf, net_id):
    account_sks = server_conf.get('account_sks', {})
    account_sks_from_server_conf = account_sks.get(net_id, [])
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
            comp_pk_str = get_compressed_pk_str(gen_node_sk(node_name, server_conf))
            return f'{comp_pk_str}:{node["server"]}:{node["tcp_port"]}'

def gen_zjnodes(server_conf: dict, zjnodes_folder):
    if zjnodes_folder.endswith('/'):
        zjnodes_folder = zjnodes_folder[:-1]
    
    root_boostrap_strs = [_get_bootstrap_str(node['name'], server_conf) for node in server_conf['nodes'] if node['net'] == 2]
    root_boostrap_str = ','.join(root_boostrap_strs)

    for node in server_conf['nodes']:
        sk = gen_node_sk(node['name'], server_conf)
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

def gen_genesis_sh_file(server_conf: dict, file_path, datadir='/root'):
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

    code_str = f"""
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

# nobuild: no build & no genesis block
# noblock: build & no genesis block
NO_BUILD=0
if [ -n $2 ] && [ $2 = "nobuild" ]
then
    NO_BUILD="nobuild"
fi

if [ -n $2 ] && [ $2 = "noblock" ]
then
    NO_BUILD="noblock"
fi

if test $NO_BUILD = 0
then
	sh build.sh a $TARGET	
elif test $NO_BUILD = "noblock"
then
	sh build.sh a $TARGET
	sudo mv -f {datadir}/zjnodes/zjchain /tmp/
else
	sudo mv -f {datadir}/zjnodes/zjchain /tmp/
fi

sudo rm -rf {datadir}/zjnodes
sudo cp -rf ./zjnodes {datadir}
sudo cp -rf ./deploy {datadir}
sudo cp ./fetch.sh {datadir}
rm -rf {datadir}/zjnodes/*/zjchain {datadir}/zjnodes/*/core* {datadir}/zjnodes/*/log/* {datadir}/zjnodes/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf {datadir}/zjnodes/zjchain
	sudo mv -f /tmp/zjchain {datadir}/zjnodes/
fi
"""

    net_keys = []
    for net_id in net_ids:
        key = 'root' if net_id == 2 else 'shard' + str(net_id)
        net_keys.append(key)
        value = net_names_str_map[net_id]
        code_str += f"{key}=({value})\n"
    code_str += f"nodes=({all_names_str})\n"

    code_str += f"""
for node in "${{nodes[@]}}"; do
    mkdir -p "{datadir}/zjnodes/${{node}}/log"
    # cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb {datadir}/zjnodes/${{node}}/conf
    # cp -rf ./zjnodes/zjchain/conf/log4cpp.properties {datadir}/zjnodes/${{node}}/conf
done
cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb {datadir}/zjnodes/zjchain
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties {datadir}/zjnodes/zjchain/conf
mkdir -p {datadir}/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain {datadir}/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml {datadir}/zjnodes/zjchain/genesis.yml

# for node in "${{nodes[@]}}"; do
    # sudo cp -rf ./cbuild_$TARGET/zjchain {datadir}/zjnodes/${{node}}
# done
sudo cp -rf ./cbuild_$TARGET/zjchain {datadir}/zjnodes/zjchain

"""
    code_str += """
if test $NO_BUILD = 0
then
"""
    code_str += f"    cd {datadir}/zjnodes/zjchain && ./zjchain -U\n"
    for net_id in net_ids:
        if net_id == 2:
            continue
        arg_str = '-S ' + str(net_id)
        code_str += f"    cd {datadir}/zjnodes/zjchain && ./zjchain {arg_str} &\n"

    code_str += "    wait\nfi\n"

    for net_id in net_ids:
        net_key = 'root' if net_id == 2 else 'shard' + str(net_id)
        db_str = 'root_db' if net_id == 2 else 'shard_db_' + str(net_id)
        code_str += f"""
#for node in "${{{net_key}[@]}}"; do
#	cp -rf {datadir}/zjnodes/zjchain/{db_str} {datadir}/zjnodes/${{node}}/db
#done

"""
        
    code_str += f"""
# 压缩 zjnodes/zjchain，便于网络传输
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


def gen_run_nodes_sh_file(server_conf: dict, file_path, build_genesis_path, tag, datadir='/root', medium_server_num=-1):
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

    code_str += f"target=$1\nno_build=$2\n"

    server0_node_names_str = ' '.join(server_node_map[server0])
    server0_pass = server_conf['passwords'].get(server0, '')
    code_str += f"""
echo "[$server0]"
sh {build_genesis_path} $target $no_build
cd {datadir} && sh -x fetch.sh 127.0.0.1 ${{server0}} '{server0_pass}' '{datadir}' {server0_node_names_str}
"""
    

    code_str += f"""echo "==== 同步中继服务器 ====" \n"""

    shard_nodes_map0 = {}

    medium_server_names = [] # 中继服务器
    
    # 第一层，先从 server0 同步到中继服务器
    for server_name, server_ip in server_name_map.items():
        if server_name == 'server0':
            continue
        server_node_names_str = ' '.join(server_node_map[server_ip])
        server_pass = server_conf['passwords'].get(server_ip, '')

        code_str += f"""
(
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -o StrictHostKeyChecking=no root@${server_name} <<EOF
mkdir -p {datadir};
rm -rf {datadir}/zjnodes;
sshpass -p '{server0_pass}' scp -o StrictHostKeyChecking=no root@"${{server0}}":{datadir}/fetch.sh {datadir}/
cd {datadir} && sh -x fetch.sh ${{server0}} ${{{server_name}}} '{server0_pass}' '{datadir}' {server_node_names_str};

EOF
) &

"""
        # 如果达到中继服务器数量，则停止
        if medium_server_num != -1:
            medium_server_names.append(server_name)
            if len(medium_server_names) >= medium_server_num:
                break
        
    code_str += "wait\n"

    # 第二层，从中继服务器同步到其他服务器
    if len(medium_server_names) > 0:
        code_str += f"""echo "==== 同步其他服务器 ====" \n"""
        for idx, (server_name, server_ip) in enumerate(server_name_map.items()):
            if server_name == 'server0':
                continue
            if server_name in medium_server_names:
                continue
            server_node_names_str = ' '.join(server_node_map[server_ip])
            server_pass = server_conf['passwords'].get(server_ip, '')

            medium_server = medium_server_names[idx % len(medium_server_names)]
            medium_server_ip = server_name_map[medium_server]
            medium_server_pass = server_conf['passwords'].get(medium_server_ip, '')

            code_str += f"""
(
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -o StrictHostKeyChecking=no root@${server_name} <<EOF
mkdir -p {datadir};
rm -rf {datadir}/zjnodes;
sshpass -p '{server0_pass}' scp -o StrictHostKeyChecking=no root@"${{server0}}":{datadir}/fetch.sh {datadir}/
cd {datadir} && sh -x fetch.sh ${{{medium_server}}} ${{{server_name}}} '{medium_server_pass}' '{datadir}' {server_node_names_str};

EOF
) &

"""
            
        code_str += "wait\n"   


    code_str += f"""
(
echo "[$server0]"
for n in {server0_node_names_str}; do
    ln -s {datadir}/zjnodes/zjchain/GeoLite2-City.mmdb {datadir}/zjnodes/${{n}}/conf
    ln -s {datadir}/zjnodes/zjchain/conf/log4cpp.properties {datadir}/zjnodes/${{n}}/conf
    ln -s {datadir}/zjnodes/zjchain/zjchain {datadir}/zjnodes/${{n}}
done
) &

""" 


    for server_name, server_ip in server_name_map.items():
        if server_name == 'server0':
            continue
        server_node_names_str = ' '.join(server_node_map[server_ip])
        server_pass = server_conf['passwords'].get(server_ip, '')

        shard_nodes_map = {}
        for nodename in server_node_map[server_ip]:
            s = get_shard_by_nodename(nodename)
            if not shard_nodes_map.get(s):
                shard_nodes_map[s] = [nodename]
            else:
                shard_nodes_map[s].append(nodename)

        code_str += f"""
(
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -o StrictHostKeyChecking=no root@${server_name} <<EOF
for n in {server_node_names_str}; do
    ln -s {datadir}/zjnodes/zjchain/GeoLite2-City.mmdb {datadir}/zjnodes/\${{n}}/conf
    ln -s {datadir}/zjnodes/zjchain/conf/log4cpp.properties {datadir}/zjnodes/\${{n}}/conf
    ln -s {datadir}/zjnodes/zjchain/zjchain {datadir}/zjnodes/\${{n}}
done
"""

        for s, nodes in shard_nodes_map.items():
            nodes_name_str = ' '.join(nodes)
            dbname = get_dbname_by_shard(s)
            code_str += f"""
for n in {nodes_name_str}; do
    cp -rf {datadir}/zjnodes/zjchain/{dbname} {datadir}/zjnodes/\${{n}}/db
done
"""

        code_str += f"""
EOF
) &

"""    
             
    code_str += """(\n"""
    for nodename in server_node_map[server0]:
        s = get_shard_by_nodename(nodename)
        if not shard_nodes_map0.get(s):
            shard_nodes_map0[s] = [nodename]
        else:
            shard_nodes_map0[s].append(nodename)

    for s, nodes in shard_nodes_map0.items():
        nodes_name_str = ' '.join(nodes)
        dbname = get_dbname_by_shard(s)
        code_str += f"""
for n in {nodes_name_str}; do
    cp -rf {datadir}/zjnodes/zjchain/{dbname} {datadir}/zjnodes/${{n}}/db
done
"""    
        
    code_str += """) &\n"""

    code_str += "wait\n"
        
    code_str += f"""
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
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd {datadir}/zjnodes/r1/ && nohup ./zjchain -f 1 -g 0 r1 {tag}> /dev/null 2>&1 &

sleep 3
"""
    
    server0_nodes = []
    for server_name, server_ip in server_name_map.items():
        if server_name == 'server0':
            server0_nodes = server_node_map[server_ip]
        else:
            server_nodes_str = ' '.join(server_node_map[server_ip])
        
            server_pass = server_conf['passwords'].get(server_ip, '')
            code_str += f"""
echo "[${server_name}]"
sshpass -p '{server_pass}' ssh -f -o StrictHostKeyChecking=no root@${server_name} bash -c "'\\
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64; \\
for node in {server_nodes_str}; do \\
    cd {datadir}/zjnodes/\$node/ && nohup ./zjchain -f 0 -g 0 \$node {tag}> /dev/null 2>&1 &\\
done \\
'"

    """      
            
    server0_nodes.remove('r1')
    server_nodes_str = ' '.join(server0_nodes)
    server_pass = server_conf['passwords'].get(server_ip, '')
    code_str += f"""
echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in {server_nodes_str}; do
cd {datadir}/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node {tag}> /dev/null 2>&1 &
done

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


def get_shard_by_nodename(node_name):
    if node_name.startswith('r'):
        return 2
    
    return int(node_name[1])
    

def get_dbname_by_node(node_name):
    return get_dbname_by_shard(get_shard_by_nodename(node_name))

def get_dbname_by_shard(shard):
    if shard == 2:
        return 'root_db'
    
    return f'shard_db_{shard}'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', help='nodes_conf.yml 文件位置', default='')
    parser.add_argument('--datadir', help='datadir', default='/root')
    parser.add_argument('--medium_num', help='中继服务器数量', default='-1', type=int)
    args = parser.parse_args()
    if args.config == '':
        args.config = './nodes_conf.yml'

    if args.datadir.endswith('/'):
        args.datadir = args.datadir[:-1]

    tag = args.datadir.split('/')[-1]

    file_path = args.config
    server_conf = parse_server_yml_file(file_path)
    build_genesis_path = './build_genesis.sh'
    gen_zjnodes(server_conf, "./zjnodes")
    gen_genesis_yaml_file(server_conf, "./conf/genesis.yml")
    gen_genesis_sh_file(server_conf, build_genesis_path, datadir=args.datadir)
    gen_run_nodes_sh_file(server_conf, "./deploy_genesis.sh", build_genesis_path, tag=tag, datadir=args.datadir, medium_server_num=args.medium_num)
    modify_shard_num_in_src_code(server_conf)

if __name__ == '__main__':
    main()