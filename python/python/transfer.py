###############################################################################
# coding: utf-8
#
###############################################################################

import json
import shardora_api
import sys
import time
from eth_utils import decode_hex, encode_hex
from eth_abi import encode

import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='process args')
    parser.add_argument('--private_key', '-p', type=str, help='私钥， 默认从../init_accounts3 获取第一个')
    parser.add_argument('--to', '-t', type=str, help='目标地址，默认默认从../init_accounts3 获取第二个')
    parser.add_argument('--amount', '-a', type=int, help='默认99999')
    args = parser.parse_args()
    private_key = None
    to = None
    from_address = None
    amount = 99999
    with open("../init_accounts3", "r") as f:
        private_key = f.readline().strip().split("\t")[0]
        tmp_to_pk = f.readline().strip().split("\t")[0]
        to = shardora_api.get_keypair(bytes.fromhex(tmp_to_pk)).account_id
        from_address = shardora_api.get_keypair(bytes.fromhex(private_key)).account_id
        
    if args.private_key:
        private_key = args.private_key
        from_address = shardora_api.get_keypair(bytes.fromhex(private_key)).account_id

    if args.to:
        to = args.to

    if args.amount:
        amount = args.amount

    # 获取转账前from和to的账户余额
    res = shardora_api.get_account_info(from_address)
    if res.status_code != 200:
        print(f"get from info failed: {from_address}")
    else:
        json_res = json.loads(res.text)
        balance = json_res['balance']
        print(f"before transfer get {from_address} balance: {balance}")

    res = shardora_api.get_account_info(to)
    if res.status_code != 200:
        print(f"get to info failed: {to}")
    else:
        json_res = json.loads(res.text)
        balance = json_res['balance']
        print(f"before transfer get {to} balance: {balance}")

    res = shardora_api.transfer(
        private_key,
        to,
        amount,
        0,
        "",
        "",
        "",
        "",
        "",
        0,
        check_gid_valid=True)
            
    if not res:
        print(f"transfer failed: {res}")
        sys.exit(1)

    time.sleep(10)
    # 获取转账后from和to的账户余额
    res = shardora_api.get_account_info(from_address)
    if res.status_code != 200:
        print(f"get from info failed: {from_address}")
    else:
        json_res = json.loads(res.text)
        balance = json_res['balance']
        print(f"after transfer get {from_address} balance: {balance}")

    res = shardora_api.get_account_info(to)
    if res.status_code != 200:
        print(f"get to info failed: {to}")
    else:
        json_res = json.loads(res.text)
        balance = json_res['balance']
        print(f"after transfer get {to} balance: {balance}")

    print(f"transfer success from {private_key} to {to} amount {amount}")
        