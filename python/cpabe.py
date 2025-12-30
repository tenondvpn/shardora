###############################################################################
# coding: utf-8
#
###############################################################################

import sha3
import configparser
import shardora_api
import no_block_sys_cmd
import sys
import time
import copy
from eth_utils import decode_hex, encode_hex
from eth_abi import encode
from clickhouse_driver import Client

if __name__ == "__main__":
    print("hello python script.")
    config = configparser.ConfigParser()
    config.read("./run.conf")
    # 读取databaas平台配置参数
    prikey_base = config.get("run", "prikey_base")
    start = int(config.get("run", "start"))
    end = int(config.get("run", "end"))
    contract_address = config.get("run", "contarct_address")
    id_info = config.get("run", "id_info")
    output = config.get("run", "output")

    cmd = no_block_sys_cmd.NoBlockSysCommand()
    check_gids = ""
    private_keys = {}
    gid_dict = {}
    gids = []
    for i in range(start, end):
        stdout, stderr, ret = cmd.run_once(f"mkdir -p {output} && chmod 755 ./pkicli && ./pkicli -t 1 -d {output}/all_{i} -f {output}/public_key_{i}")
        if ret != 0:
            sys.exit(1)

        # print(stdout.decode())
        # print(stderr.decode())
        stdout, stderr, ret = cmd.run_once(f"cat {output}/public_key_{i}")
        if ret != 0:
            sys.exit(1)
            
        pk = encode_hex(stdout.decode().strip())
        # print(stdout.decode())
        # print(stderr.decode())
        # print(pk)
        tmp_key = str(i)
        private_key = prikey_base[0:len(prikey_base) - len(tmp_key)] + tmp_key
        id = shardora_api.keccak256_str('cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848' + contract_address + id_info + private_key)
        # print(id)
        func_param = shardora_api.keccak256_str(
            "AddUserPublicKey(bytes32,bytes)")[:8] + encode_hex(
                encode(['bytes32', 'bytes'], 
                [decode_hex(id), decode_hex(pk)]))[2:]
        gid = shardora_api.gen_gid()
        gids.append(gid)
        private_keys[gid] = private_key
        gid_dict[gid] = False
        check_gids += "'" + gid + "',"
        res = shardora_api.transfer(
            private_key,
            contract_address,
            0,
            8,
            gid,
            "",
            func_param,
            "",
            "",
            0,
            check_gid_valid=False)
        
        # time.sleep(0.1)
        # print(f"res: {res}")
        if not res:
            sys.exit(1)
