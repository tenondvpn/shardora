import time

import yaml
import argparse
import requests
import concurrent.futures
import json
import subprocess




def check_addresses(addresses):
    allcount=len(addresses)
    def check_address(address):
        res = address
        try:
            # response = requests.post(f'http://10.200.48.58:8301/query_account?address={address}')
            response = requests.post(f'http://127.0.0.1:8301/query_account?address={address}')
            if response.status_code != 200 or response.json() is None:
                print(f"Failed address: {address} , response:{response}")
                return address
            bdata = response.json()
            if response.json().get("balance") is not None:
                print(f"Succeeded address: {address} , balance:{bdata.get('balance')}")
                res = None
        except Exception as e:
            print(f"Failed address: {address} , error:{e}")
            res = address
        return res

    # 创建一个线程池
    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        count = 1
        while addresses:
            # 使用线程池发送请求并获取结果
            results = list(executor.map(check_address, addresses))
            # 过滤出失败的地址
            print(f"Checking {count}th addresses...")
            count += 1
            failed_addresses = [result for result in results if result is not None]
            if failed_addresses:
                print(f"Some addresses failed, retrying... all:{allcount}, try:{len(results)},fail:{len(failed_addresses)}")
                # result = subprocess.run([ "/root/shardora/cbuild_Debug/txcli", "5"] + failed_addresses , capture_output=True, text=True, cwd="/root/shardora/cbuild_Debug")
                # print('Have a look at stdout:\n', result.stdout)
                # addresses = failed_addresses
                time.sleep(2)
            else:
                print(f"All {allcount} addresses succeeded.")
                break



def parse_server_yml_file(file_path: str):
    with open(file_path) as f:
        data = yaml.safe_load(f)
    return data


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--config_file', help='config_path', default='new_nodes_conf.yml', type=str)
    args = parser.parse_args()
    data = parse_server_yml_file(args.config_file)
    addrs = []
    for idx, node in enumerate(data["new_nodes"]):
        addrs.append(node['addr'])

    check_addresses(addrs)
