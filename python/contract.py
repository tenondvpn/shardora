###############################################################################
# coding: utf-8
#
###############################################################################

import json
import shardora_api
import sys
import binascii
import time
from eth_utils import decode_hex, encode_hex
from eth_abi import encode

import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='process args')
    parser.add_argument('--private_key', '-p', type=str, help='私钥， 默认从./init_accounts3 获取第一个')
    parser.add_argument('--to', '-t', type=str, help='目标地址，默认随机值')
    parser.add_argument('--amount', '-a', type=int, help='转账金额默认0')
    parser.add_argument('--prepayment', '-g', type=int, help='预置gas费默认0')
    parser.add_argument('--sol', '-s', type=str, help='合约文件')
    parser.add_argument('--query', '-q', type=str, help='查询合约的函数名')
    parser.add_argument('--function', '-f', type=str, help='调用合约的函数名')
    parser.add_argument('--function_types', '-c', type=str, help='调用合约的函数参数类型列表，如果function为空，则为构造函数列表')
    parser.add_argument('--function_args', '-d', type=str, help='调用合约的函数参数值列表，如果function为空，则为构造函数列表')
    parser.add_argument('--library', '-l', type=bool, help='创建library则为true')
    parser.add_argument('--libraries', '-m', type=str, help='合约依赖的library库地址')
    parser.add_argument('--qyery_type', '-i', type=int, help='合约查询函数type')
    parser.add_argument('--ripemd_act', '-x', type=str, help='ripemd自定函数操作命令')
    parser.add_argument('--ripemd_key', '-y', type=str, help='ripemd自定函数操作key')
    parser.add_argument('--ripemd_val', '-z', type=str, help='ripemd自定函数操作value')

    args = parser.parse_args()
    private_key = None
    to = None
    from_address = None
    amount = 0
    prepayment = 0
    function = ""
    function_types = []
    function_args = []
    sol_file = None
    create_library = False
    query_func = None
    qyery_type = 0
    ripemd_act = ""
    ripemd_key = ""
    ripemd_val = ""

    if args.ripemd_act:
        ripemd_act = args.ripemd_act

    if args.ripemd_key:
        ripemd_key = args.ripemd_key

    if args.ripemd_val:
        ripemd_val = args.ripemd_val
        
    # with open("./init_accounts3", "r") as f:
    #     private_key = f.readline().strip().split("\t")[0]
    #     from_address = shardora_api.get_keypair(bytes.fromhex(private_key)).account_id
        
    if args.private_key:
        private_key = args.private_key
        from_address = shardora_api.get_keypair(bytes.fromhex(private_key)).account_id

    if args.to:
        to = args.to

    if args.amount:
        amount = args.amount

    if args.prepayment:
        prepayment = args.prepayment

    if args.function:
        function = args.function

    if args.function_types:
        function_types = args.function_types.split(",")

    if args.function_args:
        function_args = args.function_args.split(",")

    if args.library:
        create_library = args.library

    if args.query:
        query_func = args.query

    if args.qyery_type:
        qyery_type = args.qyery_type

    libraries = ""
    if args.libraries:
        libraries = args.libraries

    if len(function_types) != len(function_args):
        print(f"invalid function types {function_types} and function args {function_args}")
        sys.exit(1)

    if args.sol:
        sol_file = args.sol

    tmp_function_args = []
    for i in range(len(function_types)):
        arg_type = function_types[i]
        if not arg_type.endswith('[]'):
            if arg_type.startswith('bytes') or arg_type == 'address':
                tmp_function_args.append(decode_hex(function_args[i]))
            elif arg_type == 'string':
                tmp_function_args.append(function_args[i])
            elif arg_type == 'bool':
                if function_args[i].lower() == 'false' or function_args[i] == "0":
                    tmp_function_args.append(False)
                else:
                    tmp_function_args.append(True)
            elif arg_type == 'ripemd':
                function_types[i] = 'bytes'
                str_len = str(len(ripemd_key))
                if len(ripemd_key) < 10:
                    str_len = '0' + str_len
                bytes_param = ripemd_act + str_len + ripemd_key + ripemd_val
                tmp_function_args.append(bytes_param)
            else:
                tmp_function_args.append(int(function_args[i]))
        else:
            if arg_type.startswith('bytes') or arg_type.startswith('address'):
                items = function_args[i].split('-')
                tmp_arr = []
                for item in items:
                    tmp_arr.append(decode_hex(item))

                tmp_function_args.append(tmp_arr)
            elif arg_type.startswith('string'):
                items = function_args[i].split('-')
                tmp_function_args.append(items)
            elif arg_type.startswith('bool'):
                items = function_args[i].split('-')
                tmp_arr = []
                for item in items:
                    if item.lower() == 'false' or item == "0":
                        tmp_arr.append(False)
                    else:
                        tmp_arr.append(True)

                tmp_function_args.append(tmp_arr)
            else:
                items = function_args[i].split('-')
                tmp_arr = []
                for item in items:
                    tmp_arr.append(int(item))
                        
                tmp_function_args.append(tmp_arr)


    print(f"tmp_function_args: {tmp_function_args}, function_types: {function_types}")

    if query_func is not None:
        res = shardora_api.query_contract_function(
            private_key=private_key, 
            contract_address=to, 
            function=query_func,
            types_list=function_types,
            params_list=tmp_function_args,
            call_type=qyery_type)
        if res.status_code != 200:
            print(f"query function failed status: {res.status_code}, text: {res.text}")
            sys.exit(1)

        print(f"query function success: {len(res.text)} {res.text}")
        sys.exit(0)

    if sol_file is None and function == "":
        if to is not None and prepayment > 0:
            res = shardora_api.transfer(
                private_key,
                to,
                0,
                7,
                "",
                "",
                "",
                "",
                "",
                prepayment=prepayment,
                check_gid_valid=True)
            if not res:
                print("prepayment contract failed!")
                sys.exit(1)
            else:
                print("prepayment contract success!")
                sys.exit(0)

        print(f"invalid params sol_file is None and function is None")
        sys.exit(1)

    func_param = shardora_api.keccak256_str(
        f"{function}({args.function_types})")[:8] + encode_hex(encode(function_types, tmp_function_args))[2:]
    if function == "":
        contract_address = shardora_api.deploy_contract(
            private_key,
            0,
            sol_file,
            function_types,
            tmp_function_args,
            prepayment=prepayment,
            check_gid_valid=True,
            is_library=create_library,
            in_libraries=libraries,
            contract_address=to)
        if contract_address is None:
            print(f"contract create failed!")
            sys.exit(1)
        else:
            print(f"create contract success {contract_address}")
    else:
        res = shardora_api.transfer(
            private_key,
            to,
            0,
            8,
            "",
            "",
            func_param,
            "",
            "",
            0,
            check_gid_valid=True,
            gas_limit=90000000)
        if not res:
            print("call contract failed!")
            sys.exit(1)
        else:
            print("call contract success!")

        