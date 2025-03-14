###############################################################################
# coding: utf-8
#
###############################################################################

import configparser
import json
import shardora_api
import time
import random
import sys
from eth_utils import decode_hex, encode_hex
from eth_abi import encode


if __name__ == "__main__":
    config = configparser.ConfigParser()
    config.read("./run.conf")
    # 读取databaas平台配置参数
    input = config.get("run", "input").strip()
    price = int(config.get("run", "price"))
    user_start = int(config.get("run", "user_start"))
    user_end = int(config.get("run", "user_end"))
    prikey_base = config.get("run", "prikey_base")
    exchange_contarct_address = config.get("run", "exchange_contarct_address")

    # 访问确权溯源数据库获取数据
    data_for_hash = ""
    if input != "":
        with open(input, 'r', encoding='utf-8') as file:
            data_for_hash = file.read()

    gids = []
    check_gids = ""
    gid_dict = {}
    for i in range(user_start, user_end):
        for j in range(0, 10):
            random_number = i + j  # random.randrange(0, 200)
            tmp_hash = data_for_hash
            if tmp_hash == "":
                tmp_hash = shardora_api.keccak256_str(str(random_number))

            tmp_key = str(i)
            private_key = prikey_base[0:(len(prikey_base) - len(tmp_key))] + tmp_key
            func_param = shardora_api.keccak256_str(
                "PurchaseItem(bytes32,uint256)")[:8] + encode_hex(encode(
                    ['bytes32','uint256'], 
                    [decode_hex(tmp_hash),int(time.time() * 1000)]))[2:]
            gid = shardora_api.gen_gid()
            gid_dict[gid] = False
            if len(gid_dict) < 1000:
                check_gids += gid + "_"

            tmp_price = random.randrange(100000, price)
            res = shardora_api.transfer(
                private_key,
                exchange_contarct_address,
                tmp_price,
                8,
                gid,
                "",
                func_param,
                "",
                "",
                0,
                check_gid_valid=False)
            
            gids.append(gid)
    
    time.sleep(20)

    for i in range(0, 30):
        res = shardora_api.check_gid_valid({"gids": check_gids})
        print(f"res status: {res.status_code}, text: {res.text}")
        if res.status_code != 200:
            print("post check gids failed!")
        else:
            json_res = json.loads(res.text)
            if json_res["gids"] is not None:
                for gid in json_res["gids"]:
                    gid_dict[gid] = True
                # print(f"id valid: {filed[0]}", flush=True)

        check_gids = ""
        count = 0
        for gid in gid_dict:
            if not gid_dict[gid]:
                count += 1
                if count >= 1000:
                    break

                check_gids += gid + "_"

        if check_gids == "":
            start_pos = 0
            get_len = 100
            res = shardora_api.query_contract_function(
                private_key=private_key, 
                contract_address=exchange_contarct_address,
                function="GetAllItemJson",
                types_list=['uint256', 'uint256'],
                params_list=[start_pos, get_len])
            print(res.status_code)
            print(res.text)
            print(json.loads(res.text))
            sys.exit(0)

        time.sleep(6)

    for gid in gid_dict:
        if not gid_dict[gid]:
            print(f"gid invalid: {gid}")
            
    sys.exit(1)

    
        