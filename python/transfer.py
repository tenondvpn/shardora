###############################################################################
# coding: utf-8
#
###############################################################################

import shardora_api
import sys
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
    amount = 99999
    with open("../init_accounts3", "r") as f:
        private_key = f.readline().strip().split("\t")[0]
        tmp_to_pk = f.readline().strip().split("\t")[0]
        to = shardora_api.get_keypair(bytes.fromhex(tmp_to_pk)).account_id
        
    if args.private_key:
        private_key = args.private_key

    if args.to:
        to = args.to

    if args.amount:
        amount = args.amount

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

    print(f"transfer success from {private_key} to {to} amount {amount}")
        