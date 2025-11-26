import argparse
import os

import toml
import yaml
import configparser
import gen_genesis_script as genesis


def gen_new_zjnodes_conf_files(server_conf: dict, join_root_num):
    zjnodes_folder = "./zjnodes"
    bootstrap = get_root_boostrap_strs()
    bootstrap = bootstrap.replace('"', '')
    print(bootstrap)
    for node in server_conf['new_nodes']:
        addr = genesis.sk2account(node['prikey'])
        node["addr"] = addr
 


        shardora_conf = {
            'db': {
                'path': './db',
            },
            'log': {
                'path': 'log/shardora.log',
            },
            'shardora': {
                'bootstrap': bootstrap,
                'ck_ip': '127.0.0.1',
                'ck_passworkd': '',
                'ck_user': 'default',
                'country': 'NL',
                'first_node': 1 if node['name'] == 'r1' else 0,
                'http_port': node['http_port'],
                'local_ip': node['server'],
                'local_port': node['tcp_port'],
                'net_id': node['net'],
                'prikey': node['prikey'],
                '_addr': addr,
                'show_cmd': 0,
                'for_ck': False,
                'join_root': node['join_root'],
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
        filename = f'{sub_conf_folder}/shardora.conf'
        print(os.path.abspath(filename))

        with open(filename, 'w') as f:
            toml.dump(shardora_conf, f)


def gen_dispatch_coin_sh(content):
    filename = "new_nodes_dispatch_coin.sh"
    all_addr = []

    for node in content['new_nodes']:
        all_addr.append(node['addr'])

    all_addr_str = ' '.join(all_addr)

    sh_str = f"""#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
cd ./cbuild_Debug
make txcli
./txcli 5 {all_addr_str}



"""
    with open(filename, 'w') as f:
        f.write(sh_str)
    pass


def gen_nodes_conf_file(node_num_per_shard, shard_num, servers, join_root_nums):
    content = build_yam_content(node_num_per_shard, servers, shard_num, join_root_nums)

    gen_new_zjnodes_conf_files(content, join_root_nums)
    gen_new_node_deploy_sh(content)
    gen_dispatch_coin_sh(content)

    filename = f"new_nodes_conf.yml"
    with open(filename, "w") as f:
        yaml.dump(content, f)
    return


def gen_new_node_deploy_sh(server_conf):
    all_names = []
    for node in server_conf['new_nodes']:
        all_names.append('"' + node['name'] + '"')

    all_names_str = ' '.join(all_names)
    node_list = f"nodes=({all_names_str})\n"
    code_str = f"""#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
ps -ef | grep shardora | grep new_node | awk -F' ' '{{print $2}}' | xargs kill -9

{node_list}

rm -rf /root/zjnodes/new*
# (python test_accounts.py )&
for n in  "${{nodes[@]}}"; do

        mkdir -p "/root/zjnodes/${{n}}/log"
        mkdir -p "/root/zjnodes/${{n}}/conf"
        ln -s /root/zjnodes/shardora/GeoLite2-City.mmdb /root/zjnodes/${{n}}/conf/
        ln -s /root/zjnodes/shardora/conf/log4cpp.properties /root/zjnodes/${{n}}/conf/
        ln -s /root/zjnodes/shardora/shardora /root/zjnodes/${{n}}/
        cp -rf ./zjnodes/${{n}}/conf/shardora.conf /root/zjnodes/${{n}}/conf/shardora.conf
        echo "cp $n"
done

ulimit -c unlimited


for node in "${{nodes[@]}}"; do
  cd /root/zjnodes/$node/ && nohup ./shardora -f 0 -g 0 $node new_node> /dev/null 2>&1 &
  echo "start $node"

done



"""
    full_path = "new_node_deploy.sh"
    # full_path = os.path.abspath(file_path)
    print(full_path)
    with open(full_path, 'w') as f:
        f.write(code_str)


def build_yam_content(node_num_per_shard, servers, shard_num, join_root_num=0):
    

    shard_start_idx = 3
    shard_end_idx = shard_start_idx + shard_num - 1
    new_nodes = []
    prikeys = gen_prikeys(shard_num * node_num_per_shard)
    join_root_count = 0
    for shard_idx in range(shard_start_idx, shard_end_idx + 1):
        
        for node_idx in range(node_num_per_shard):
            join_root = 0
            if join_root_count < join_root_num:
                join_root_count += 1
                join_root = 1
            else:
                join_root = 2
            node_idx += 1
            node_name = f"new_{node_idx}"
            net_id = shard_idx
            if join_root == 1:
                net_id = 2
                
            new_nodes.append({
                "name": node_name,
                "net": net_id,
                "server": "",
                "http_port": get_new_http_port(node_idx, shard_idx),
                "tcp_port": get_new_tcp_port(node_idx, shard_idx),
                "join_root": join_root,
            })
    for idx, node in enumerate(new_nodes):
        server = servers[idx % len(servers)]
        node["server"] = server
        node["prikey"] = prikeys[idx]
    content = {
        'new_nodes': new_nodes,
        'prikeys': prikeys,
    }
    return content


def gen_prikeys(count):
    prikeys = []
    s = "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b3000000"

    for i in range(count):
        new_string = s[:-len(str(i))] + str(i)
        prikeys.append(new_string)
    return prikeys


def get_new_http_port(node_idx, net_id):
    return 7000 + net_id * 100 + node_idx


def get_new_tcp_port(node_idx, net_id):
    return 20000 + net_id * 1000 + node_idx


def get_root_boostrap_strs():
    filepath = "zjnodes/r1/conf/shardora.conf"
    config = configparser.ConfigParser()
    config.read(filepath)
    bootstrap = config.get('shardora', 'bootstrap')
    print(f"bootstrap: {bootstrap}")
    return bootstrap


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-n', '--node_num_per_shard', help='node_num_per_shard', type=int, default=10)
    parser.add_argument('-s', '--shard_num', help='shard_num', default=1, type=int)
    parser.add_argument('-m', '--machines', help='machines', default="127.0.0.1", type=str)
    parser.add_argument('-m0', '--machine0', help='source machine', default='127.0.0.1', type=str)
    args = parser.parse_args()

    servers = args.machines.split(",")
    print(f"shard_num $s：{args.shard_num}")
    print(f"node_num_per_shard $n：{args.node_num_per_shard}")
    print(f"servers $m：{servers}")

    join_root_nums = 0
    gen_nodes_conf_file(args.node_num_per_shard, args.shard_num, servers, join_root_nums)
