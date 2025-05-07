###############################################################################
# coding: utf-8
#
###############################################################################

import configparser
import json
import shardora_api
import sys
import time
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='process args')
    parser.add_argument('--private_key', '-p', type=str, help='私钥')
    parser.add_argument('--amount', '-a', type=int, help='转账金额默认0')
    parser.add_argument('--address_count', '-n', type=int, help='转账目标地址数')
    parser.add_argument('--chain_ip', '-i', type=int, help='转账目标机器')
    args = parser.parse_args()
    from_private_key = args.private_key
    from_address = shardora_api.get_keypair(bytes.fromhex(from_private_key)).account_id
    amount = args.amount
    prikey_base = "19997691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848000000000"
    ids = dict()
    addr_info = shardora_api.get_account_info(from_address)
    if addr_info is None:
        print(f"invalid private key {from_private_key} and get addr info failed: {from_address} ")
        sys.exit(1)

    if args.chain_ip:
        shardora_api.http_ip = args.chain_ip

    nonce = int(addr_info["nonce"]) + 1
    check_accounts_str = ""
    for i in range(0, args.address_count):
        tmp_key = str(i)
        private_key = prikey_base[0:(len(prikey_base) - len(tmp_key))] + tmp_key
        to = shardora_api.get_keypair(bytes.fromhex(private_key)).account_id
        check_accounts_str += to + "_"
        res = shardora_api.transfer(
            from_private_key,
            to,
            amount,
            nonce,
            0,
            "",
            "",
            "",
            "",
            0,
            check_tx_valid=False)
        
        nonce += 1
        ids[to] = False
        if not res:
            sys.exit(1)

        if i % 5000 == 0:
            time.sleep(4)

    time.sleep(20)
    for i in range(0, 30):
        res = shardora_api.check_accounts_valid({"addrs": check_accounts_str, "balance": amount})
        print(f"res status: {res.status_code}, text: {res.text}")
        if res.status_code != 200:
            print(f"post check gids failed status: {res.status_code}, error: {res.text}")
        else:
            json_res = json.loads(res.text)
            if json_res["addrs"] is not None:
                for addr in json_res["addrs"]:
                    ids[addr] = True

        check_accounts_str = ""
        for id in ids:
            if not ids[id]:
                check_accounts_str += id + "_"

        if check_accounts_str == "":
            sys.exit(0)

        time.sleep(6)

    for id in ids:
        if not ids[id]:
            print(f"invalid address: {id}")

    sys.exit(1)
