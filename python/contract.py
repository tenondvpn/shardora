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
from web3 import Web3
from eth_abi import decode_abi
import json
import argparse

class StructParser:
    def __init__(self, abi_path):
        with open(abi_path, 'r') as f:
            self.abi = json.load(f)
        self.struct_defs = self._build_struct_definitions()

    def _build_struct_definitions(self):
        """构建struct定义字典"""
        struct_dict = {}
        for item in self.abi:
            if item.get('type') == 'struct':
                struct_dict[item['name']] = {
                    'fields': [(field['name'], field['type']) for field in item['components']],
                    'components': item['components']
                }
        return struct_dict

    def _get_field_types(self, components):
        """递归获取字段类型"""
        types = []
        for comp in components:
            if comp['type'].startswith('tuple'):
                types.append(tuple(self._get_field_types(comp['components'])))
            else:
                types.append(comp['type'])
        return types

    def _parse_nested_data(self, data, components):
        """递归解析嵌套数据"""
        if not isinstance(data, (tuple, list)):
            return data
            
        result = {}
        for i, comp in enumerate(components):
            field_name = comp['name']
            if comp['type'].startswith('tuple'):
                result[field_name] = self._parse_nested_data(data[i], comp['components'])
            else:
                result[field_name] = data[i]
        return result

    def parse_struct_return(self, struct_name, result):
        """
        调用合约函数并解析返回的struct数据（支持嵌套）
        参数:
        - struct_name: 返回的struct类型名
        - args: 调用函数需要的参数
        """
        try:
            if struct_name not in self.struct_defs:
                raise ValueError(f"Struct {struct_name} not found in ABI")

            # 获取struct定义
            struct_info = self.struct_defs[struct_name]
            field_types = self._get_field_types(struct_info['components'])

            # 解码数据
            if isinstance(result, (list, tuple)) and len(result) == 1:
                result = result[0]  # 处理单返回值的情况
            decoded_data = decode_abi(field_types, result)

            # 解析嵌套结构
            parsed_result = self._parse_nested_data(decoded_data, struct_info['components'])
            
            return parsed_result

        except Exception as e:
            return {"error": str(e)}

# # 使用示例
# def main():
#     # 配置参数
#     contract_address = "0xYourContractAddressHere"
#     abi_path = "path/to/your/contract.abi"
#     rpc_url = "http://your.ethereum.node:8545"

#     # 创建解析器实例
#     parser = StructParser(contract_address, abi_path, rpc_url)

#     # 示例：假设有个函数getProfile返回嵌套结构体
#     # Solidity定义可能是：
#     # struct Info {
#     #     string description;
#     #     uint256 score;
#     # }
#     # struct Profile {
#     #     uint256 id;
#     #     string name;
#     #     Info info;
#     #     bool isActive;
#     # }
    
#     result = parser.parse_struct_return(
#         function_name="getProfile",
#         struct_name="Profile",
#         1  # 假设函数需要一个uint256参数
#     )

#     # 输出结果
#     if "error" in result:
#         print(f"Error: {result['error']}")
#     else:
#         print("Parsed nested struct data:")
#         print(json.dumps(result, indent=2))

# if __name__ == "__main__":
#     main()
# ```

# 这个更新版本增加了以下功能：

# 1. **嵌套支持**：
#    - `_build_struct_definitions`: 预处理所有struct定义
#    - `_get_field_types`: 递归获取嵌套结构的类型定义
#    - `_parse_nested_data`: 递归解析嵌套数据结构

# 2. **改进的解析逻辑**：
#    - 支持任意层级的struct嵌套
#    - 自动将tuple转换为字典格式
#    - 处理单返回值和多返回值的情况

# 对应的ABI示例：
# ```json
# [
#     {
#         "type": "struct",
#         "name": "Info",
#         "components": [
#             {"name": "description", "type": "string"},
#             {"name": "score", "type": "uint256"}
#         ]
#     },
#     {
#         "type": "struct",
#         "name": "Profile",
#         "components": [
#             {"name": "id", "type": "uint256"},
#             {"name": "name", "type": "string"},
#             {"name": "info", "type": "tuple", "components": [
#                 {"name": "description", "type": "string"},
#                 {"name": "score", "type": "uint256"}
#             ]},
#             {"name": "isActive", "type": "bool"}
#         ]
#     },
#     {
#         "type": "function",
#         "name": "getProfile",
#         "inputs": [{"name": "userId", "type": "uint256"}],
#         "outputs": [{"type": "tuple", "components": [
#             {"name": "id", "type": "uint256"},
#             {"name": "name", "type": "string"},
#             {"name": "info", "type": "tuple", "components": [
#                 {"name": "description", "type": "string"},
#                 {"name": "score", "type": "uint256"}
#             ]},
#             {"name": "isActive", "type": "bool"}
#         ]}]
#     }
# ]
# ```

# 输出示例：
# ```json
# {
#   "id": 1,
#   "name": "John",
#   "info": {
#     "description": "Test user",
#     "score": 100
#   },
#   "isActive": true
# }
# ```

# 主要改进：
# 1. **灵活性**：可以处理任意深度的嵌套struct
# 2. **鲁棒性**：增加了错误处理和类型检查
# 3. **易用性**：返回自然的字典结构，便于后续处理

# 使用时只需确保：
# 1. ABI文件中正确定义了所有的struct
# 2. 传入的struct_name与ABI中的定义一致
# 3. 提供的函数参数与合约要求匹配

# 这个实现应该能满足大多数嵌套struct的解析需求。如果需要支持更复杂的情况（如数组或映射），可以进一步扩展。
        
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='process args')
    parser.add_argument('--private_key', '-p', type=str, help='私钥， 默认从./init_accounts3 获取第一个')
    parser.add_argument('--to', '-t', type=str, help='目标地址，默认随机值')
    parser.add_argument('--amount', '-a', type=int, help='转账金额默认0')
    parser.add_argument('--prepayment', '-g', type=int, help='预置gas费默认0')
    parser.add_argument('--sol', '-s', type=str, help='合约文件')
    parser.add_argument('--query', '-q', type=str, help='查询合约的函数名')
    parser.add_argument('--query_result', '-r', type=str, help='查询合约的返回值类型列表')
    parser.add_argument('--abi', '-b', type=str, help='查询合约的abi文件')
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
    abi = None
    if args.ripemd_act:
        ripemd_act = args.ripemd_act

    if args.ripemd_key:
        ripemd_key = args.ripemd_key

    if args.ripemd_val:
        ripemd_val = args.ripemd_val

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

    if args.abi:
        abi = args.abi

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
                tmp_function_args.append(bytes(bytes_param, encoding='utf-8'))
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

        if abi is not None:
            parser = StructParser(abi)
            result = parser.parse_struct_return(
                struct_name="AssetData",
                result=res.text)
            print(f"query function success: {len(res.text)} {result}")
            
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

    function_types_str = ','.join(function_types)
    func_param = shardora_api.keccak256_str(
        f"{function}({function_types_str})")[:8] + encode_hex(encode(function_types, tmp_function_args))[2:]
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

        