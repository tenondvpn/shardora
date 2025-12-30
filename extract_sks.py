import toml
import os
import yaml

def get_first_level_subdirectories(directory):
    # 获取目录下的所有第一层子文件夹
    return [os.path.join(directory, d) for d in os.listdir(directory) if os.path.isdir(os.path.join(directory, d))]


if __name__ == '__main__':
    folder = './zjnodes'
    node_names = [subfolder.split('/')[-1] for subfolder in get_first_level_subdirectories(folder)]
    if not node_names:
        exit(0)
    
    ret = {'node_sks': {}}
    for node_name in node_names:
        with open(f'{folder}/{node_name}/conf/shardora.conf', 'r') as f:
            if node_name == 'shardora':
                continue
            data = toml.load(f)
            if not data.get('shardora'):
                continue
            if not data['shardora'].get('prikey'):
                continue
            ret['node_sks'][node_name] = data['shardora']['prikey']
            
    with open('node_sks.yml', 'w') as f:
        yaml.dump(ret, f)
    
