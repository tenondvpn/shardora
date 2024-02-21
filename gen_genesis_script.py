from eth_keys import keys, datatypes
import sha3
from secp256k1 import PrivateKey, PublicKey
from eth_utils import decode_hex, encode_hex
from ecdsa import SigningKey, SECP256k1

import os
import yaml
import toml

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

    for net_key in net_keys:
        code_str += f"""
for node in "${{{net_key}[@]}}"; do
	cp -rf /root/zjnodes/zjchain/root_db /root/zjnodes/${{node}}/db
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


def main():
    file_path = "./servers.yml"
    server_conf = parse_server_yml_file(file_path)
    gen_zjnodes(server_conf, "./zjnodes")
    gen_genesis_yaml_file(server_conf, "./conf/genesis.yml")
    gen_genesis_sh_file(server_conf, "./genesis.sh")

if __name__ == '__main__':
    main()