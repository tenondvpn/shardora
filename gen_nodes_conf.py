import yaml
import argparse

def gen_nodes_conf_file(node_num_per_shard, shard_num, servers, root_node_num, server0):
    shard_start_idx = 3
    shard_end_idx = shard_start_idx + shard_num - 1
    nodes = []

    for root_node_idx in range(root_node_num):
        root_node_idx += 1
        node_name = f"r{root_node_idx}"
        nodes.append({
            "name": node_name,
            "net": 2,
            "server": "",
            "http_port": get_http_port(root_node_idx, 2),
            "tcp_port": get_tcp_port(root_node_idx, 2),
        })

    for shard_idx in range(shard_start_idx, shard_end_idx + 1):
        for node_idx in range(node_num_per_shard):
            node_idx += 1
            node_name = f"s{shard_idx}_{node_idx}"
            nodes.append({
                "name": node_name,
                "net": shard_idx,
                "server": "",
                "http_port": get_http_port(node_idx, shard_idx),
                "tcp_port": get_tcp_port(node_idx, shard_idx),
            })

    for idx, node in enumerate(nodes):
        if node["name"] == 'r1':
            node["server"] = server0
            continue
        server = servers[idx % len(servers)]
        node["server"] = server

    content = {
        'nodes': nodes,
    }    

    with open(f"nodes_conf_n{node_num_per_shard}_s{shard_num}_m{len(servers)}.yml", "w") as f:
        yaml.dump(content, f)

    return

def get_http_port(node_idx, net_id):
    return 8000 + net_id * 100 + node_idx


def get_tcp_port(node_idx, net_id):
    return 10000 + net_id * 1000 + node_idx


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-n', '--node_num_per_shard', help='node_num_per_shard', type=int, default=3)
    parser.add_argument('-s', '--shard_num', help='shard_num', default=2, type=int)
    parser.add_argument('-m', '--machines', help='machines', default="127.0.0.1", type=str)
    parser.add_argument('-r', '--root_node_num', help='root node num', default=3, type=int)
    parser.add_argument('-m0', '--machine0', help='source machine', default='', type=str)
    args = parser.parse_args()

    servers = args.machines.split(",")
    gen_nodes_conf_file(args.node_num_per_shard, args.shard_num, servers, args.root_node_num, args.machine0)
