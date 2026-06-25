#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Uniswap V3 Core (Solidity 0.8.20) 合约部署测试脚本
用于在 Shardora 链上部署和测试升级后的 Uniswap V3 合约
"""

import struct
import requests
import hashlib
import json
import time
import os
import math
from enum import IntEnum
import solcx
from solcx import compile_source, install_solc, compile_files
import eth_abi

from Crypto.Hash import keccak
from eth_utils import to_checksum_address
from ecdsa import SigningKey, SECP256k1
from ecdsa.util import sigencode_string_canonize

# 安装 Solidity 0.8.20
try:
    install_solc('0.8.20')
    solcx.set_solc_version('0.8.20')
    print("[Info] Solidity 0.8.20 installed and set as default")
except Exception as e:
    print(f"[Warning] Failed to install solc 0.8.20: {e}")

class MessageHandleStatus(IntEnum):
    kConsensusSuccess = 0
    kMessageHandle = 10001
    kMessageHandleError = 10002
    kTxAccept = 10003
    kTxInvalidSignature = 10004
    kTxInvalidAddress = 10005
    kTxPoolFullReject = 10006
    kTxUserNonceInvalid = 10007
    kUnknown = 10008
    kRequestInvalid = 10009
    kNotExists = 10010

class ShardoraClient:
    """Shardora 链客户端"""
    def __init__(self, host, port):
        self.base_url = f"http://{host}:{port}"
        self.tx_url = f"{self.base_url}/transaction"
        self.query_url = f"{self.base_url}/query_account"
        self.receipt_url = f"{self.base_url}/transaction_receipt"
        self.query_contract_url = f"{self.base_url}/query_contract"

    def _uint64_to_bytes(self, val):
        return struct.pack('<Q', val)

    def _hex_to_bytes(self, hex_str):
        if hex_str.startswith('0x'): 
            hex_str = hex_str[2:]
        return bytes.fromhex(hex_str)

    def get_address(self, private_key_hex):
        """从私钥派生地址"""
        if private_key_hex.startswith('0x'): 
            private_key_hex = private_key_hex[2:]
        sk = SigningKey.from_string(bytes.fromhex(private_key_hex), curve=SECP256k1)
        pub_key = sk.verifying_key.to_string("uncompressed")[1:]
        
        k = keccak.new(digest_bits=256)
        k.update(pub_key)
        return k.digest()[-20:].hex()

    def get_balance(self, address: str) -> int:
        """获取账户余额"""
        try:
            resp = requests.post(self.query_url, data={"address": address}, timeout=5)
            if resp.status_code == 200:
                return int(resp.json().get("balance", 0))
        except:
            pass
        return 0
    
    def get_nonce(self, address):
        """获取账户 nonce"""
        try:
            resp = requests.post(self.query_url, data={"address": address}, timeout=5)
            return int(resp.json().get("nonce", 0)) if resp.status_code == 200 else 0
        except: 
            return 0

    def compute_hash(self, nonce, pubkey_hex, to_hex, amount, gas_limit, gas_price, step,
                     contract_code='', input_hex='', prepayment=0, key='', val=''):
        """计算交易哈希"""
        msg = bytearray()
        msg.extend(self._uint64_to_bytes(nonce))
        msg.extend(self._hex_to_bytes(pubkey_hex))
        msg.extend(self._hex_to_bytes(to_hex))
        msg.extend(self._uint64_to_bytes(amount))
        msg.extend(self._uint64_to_bytes(gas_limit))
        msg.extend(self._uint64_to_bytes(gas_price))
        msg.extend(self._uint64_to_bytes(step))
        if contract_code: 
            msg.extend(self._hex_to_bytes(contract_code))
        if input_hex: 
            msg.extend(self._hex_to_bytes(input_hex))
        if prepayment > 0: 
            msg.extend(self._uint64_to_bytes(prepayment))
        
        if key:
            msg.extend(key.encode('utf-8'))
            if val: 
                msg.extend(val.encode('utf-8'))
        
        k = keccak.new(digest_bits=256)
        k.update(msg)
        return k.digest()

    def send_transaction_auto(self, private_key_hex, to_hex, amount=0,
                              gas_limit=5000000, gas_price=1, step=0, shard_id=0,
                              contract_code='', input_hex='', prepayment=0,
                              key='', val=''):
        """发送交易"""
        if private_key_hex.startswith('0x'): 
            private_key_hex = private_key_hex[2:]
        sk = SigningKey.from_string(bytes.fromhex(private_key_hex), curve=SECP256k1)
        pubkey_hex = sk.verifying_key.to_string("uncompressed").hex()
        my_addr = self.get_address(private_key_hex)
        
        if step == 8:
            my_addr = to_hex + my_addr
            
        nonce = self.get_nonce(my_addr) + 1
        tx_hash = self.compute_hash(nonce, pubkey_hex, to_hex, amount, gas_limit, gas_price, 
                                    step, contract_code, input_hex, prepayment, key, val)

        signature = sk.sign_digest_deterministic(tx_hash, hashfunc=hashlib.sha256, 
                                                sigencode=sigencode_string_canonize)
        
        data = {
            "nonce": str(nonce), 
            "pubkey": pubkey_hex, 
            "to": to_hex, 
            "amount": str(amount),
            "gas_limit": str(gas_limit), 
            "gas_price": str(gas_price), 
            "shard_id": str(shard_id),
            "type": str(step), 
            "sign_r": signature[0:32].hex(), 
            "sign_s": signature[32:64].hex(), 
            "sign_v": "0"
        }
        if contract_code: 
            data["bytes_code"] = contract_code
        if input_hex: 
            data["input"] = input_hex
        if prepayment > 0: 
            data["prefund"] = str(prepayment)
        if key: 
            data["key"] = key
        if val: 
            data["val"] = val

        try:
            resp = requests.post(self.tx_url, data=data, timeout=5)
            print(f"[TX] Result: {resp.text}")
            if "SignatureInvalid" in resp.text or "kSignatureInvalid" in resp.text:
                data["sign_v"] = "1"
                resp = requests.post(self.tx_url, data=data, timeout=5)
                print(f"[TX] Retry with v=1: {resp.text}")
            return tx_hash.hex()
        except Exception as e:
            print(f"[Error] Send TX failed: {e}")
            return None

    def wait_for_receipt(self, tx_hash, timeout=30):
        """等待交易回执"""
        start = time.time()
        while time.time() - start < timeout:
            try:
                resp = requests.post(self.receipt_url, data={"tx_hash": tx_hash}, timeout=2)
                if resp.status_code == 200:
                    status = resp.json().get("status")
                    print(f"[Receipt] TX {tx_hash[:16]}... status: {MessageHandleStatus(status).name}")
                    if status not in [MessageHandleStatus.kMessageHandle, MessageHandleStatus.kTxAccept]:
                        return status == MessageHandleStatus.kConsensusSuccess
            except: 
                pass
            time.sleep(1)
        print(f"[Warning] TX {tx_hash[:16]}... timeout after {timeout}s")
        return False

    def query_contract(self, from_hex, to_hex, input_hex):
        """查询合约"""
        try:
            resp = requests.post(self.query_contract_url, 
                               data={"from": from_hex, "address": to_hex, "input": input_hex}, 
                               timeout=5)
            if resp.status_code == 200:
                return resp.text
        except: 
            pass
        return None


def get_selector(signature):
    """获取函数选择器"""
    k = keccak.new(digest_bits=256)
    k.update(signature.encode('utf-8'))
    return k.digest()[:4].hex()


def calc_create2_address(sender, salt_hex, bytecode_hex):
    """计算 CREATE2 地址"""
    prefix = bytes.fromhex("ff")
    sender_bytes = bytes.fromhex(sender.replace('0x', ''))
    salt_bytes = bytes.fromhex(salt_hex.replace('0x', '').zfill(64))
    bytecode_bytes = bytes.fromhex(bytecode_hex.replace('0x', ''))

    k_code = keccak.new(digest_bits=256)
    k_code.update(bytecode_bytes)
    code_hash = k_code.digest()

    k_final = keccak.new(digest_bits=256)
    k_final.update(prefix + sender_bytes + salt_bytes + code_hash)
    raw_address = k_final.digest()
    
    return raw_address[-20:].hex().lower()


def compile_v3_contracts_0_8():
    """编译 Uniswap V3 合约 (0.8.20 版本)"""
    print("\n[Step 1] 编译 Uniswap V3 合约 (Solidity 0.8.20)...")

    # 脚本在 clipy/ 目录下运行，合约在 v3-core-0.8/contracts/
    script_dir = os.path.dirname(os.path.abspath(__file__))
    contracts_root = os.path.join(script_dir, "v3-core-0.8", "contracts")
    factory_sol = os.path.join(contracts_root, "UniswapV3Factory.sol")

    try:
        compiled = solcx.compile_files(
            [factory_sol],
            output_values=['abi', 'bin'],
            solc_version='0.8.20',
            optimize=True,
            optimize_runs=200,
            allow_paths=contracts_root,
        )

        # 找到 Factory 和 Pool 的编译结果
        factory_key = next((k for k in compiled if 'UniswapV3Factory' in k and 'Pool' not in k), None)
        pool_key    = next((k for k in compiled if 'UniswapV3Pool' in k and 'Deployer' not in k), None)

        if not factory_key:
            raise RuntimeError(f"UniswapV3Factory not found. Keys: {list(compiled.keys())}")

        factory_contract = compiled[factory_key]
        print(f"  ✓ UniswapV3Factory 编译成功")
        print(f"    - Bytecode 长度: {len(factory_contract['bin'])} chars")

        result = {'factory': factory_contract}

        if pool_key:
            pool_contract = compiled[pool_key]
            print(f"  ✓ UniswapV3Pool 编译成功")
            print(f"    - Bytecode 长度: {len(pool_contract['bin'])} chars")
            result['pool'] = pool_contract

        return result

    except Exception as e:
        print(f"  ✗ 编译失败: {e}")
        import traceback
        traceback.print_exc()
        return None


def calculate_sqrt_price_x96(price):
    """
    计算 sqrtPriceX96
    price = token1/token0 的比率
    sqrtPriceX96 = sqrt(price) * 2^96
    """
    sqrt_price = math.sqrt(price)
    sqrt_price_x96 = int(sqrt_price * (2 ** 96))
    return sqrt_price_x96


def deploy_v3_factory(client, private_key, compiled_contracts):
    """部署 UniswapV3Factory 合约"""
    print("\n[Step 2] 部署 UniswapV3Factory (0.8.20)...")
    
    factory_bytecode = compiled_contracts['factory']['bin'].strip().replace("0x", "")
    factory_abi = compiled_contracts['factory']['abi']
    
    sender = client.get_address(private_key)
    
    # 使用 CREATE2 计算地址
    salt = "01"
    factory_addr = calc_create2_address(sender, salt, factory_bytecode)
    print(f"  - 预计 Factory 地址: {factory_addr}")
    
    # 部署合约 (step=6)
    tx_hash = client.send_transaction_auto(
        private_key,
        factory_addr,
        step=6,
        contract_code=factory_bytecode,
        prepayment=20000000,  # V3 合约较大，需要更多 prepayment
        gas_limit=10000000
    )
    
    if not tx_hash:
        print("  ✗ Factory 部署交易发送失败")
        return None, None
    
    success = client.wait_for_receipt(tx_hash, timeout=60)
    if success:
        print(f"  ✓ Factory 部署成功: {factory_addr}")
        return factory_addr, factory_abi
    else:
        print(f"  ✗ Factory 部署失败")
        return None, None


def enable_fee_amounts(client, private_key, factory_addr):
    """Factory 构造函数已内置三种费率，此函数仅做提示"""
    print("\n[Step 3] 费率等级检查...")
    print("  - Factory 构造函数已内置: 500(0.05%), 3000(0.30%), 10000(1.00%)")
    print("  - 跳过重复启用")


def create_pool(client, private_key, factory_addr, token0_addr, token1_addr, fee=3000):
    """创建交易池"""
    print(f"\n[Step 4] 创建交易池 (fee: {fee/10000:.2%})...")
    
    sender = client.get_address(private_key)
    
    # 确保 token0 < token1
    if int(token0_addr, 16) > int(token1_addr, 16):
        token0_addr, token1_addr = token1_addr, token0_addr
        print(f"  - Token 地址已排序")
    
    print(f"  - Token0: {token0_addr}")
    print(f"  - Token1: {token1_addr}")
    print(f"  - Fee: {fee}")
    
    # 调用 createPool
    selector = get_selector("createPool(address,address,uint24)")
    input_hex = selector + eth_abi.encode(
        ['address', 'address', 'uint24'],
        [to_checksum_address("0x" + token0_addr), 
         to_checksum_address("0x" + token1_addr), 
         fee]
    ).hex()
    
    tx_hash = client.send_transaction_auto(
        private_key,
        factory_addr,
        step=8,
        input_hex=input_hex,
        prepayment=10000000
    )
    
    if not tx_hash:
        print("  ✗ 创建池交易发送失败")
        return None
    
    success = client.wait_for_receipt(tx_hash, timeout=45)
    if not success:
        print("  ✗ 创建池失败")
        return None
    
    # 查询池地址
    print("  - 查询池地址...")
    selector = get_selector("getPool(address,address,uint24)")
    query_input = selector + eth_abi.encode(
        ['address', 'address', 'uint24'],
        [to_checksum_address("0x" + token0_addr), 
         to_checksum_address("0x" + token1_addr), 
         fee]
    ).hex()
    
    result = client.query_contract(sender, factory_addr, query_input)
    if result:
        pool_addr = result.strip().lower().replace("0x", "")[-40:]
        print(f"  ✓ 池创建成功: {pool_addr}")
        return pool_addr
    else:
        print("  ✗ 查询池地址失败")
        return None


def initialize_pool(client, private_key, pool_addr, initial_price=1.0):
    """初始化池价格"""
    print(f"\n[Step 5] 初始化池价格 (price: {initial_price})...")
    
    # 计算 sqrtPriceX96
    sqrt_price_x96 = calculate_sqrt_price_x96(initial_price)
    print(f"  - sqrtPriceX96: {sqrt_price_x96}")
    
    # 调用 initialize
    selector = get_selector("initialize(uint160)")
    input_hex = selector + eth_abi.encode(['uint160'], [sqrt_price_x96]).hex()
    
    tx_hash = client.send_transaction_auto(
        private_key,
        pool_addr,
        step=8,
        input_hex=input_hex,
        prepayment=5000000
    )
    
    if not tx_hash:
        print("  ✗ 初始化交易发送失败")
        return False
    
    success = client.wait_for_receipt(tx_hash, timeout=30)
    if success:
        print(f"  ✓ 池初始化成功")
        return True
    else:
        print(f"  ✗ 池初始化失败")
        return False


def query_pool_state(client, sender, pool_addr):
    """查询池状态"""
    print(f"\n[Step 6] 查询池状态...")
    
    # 查询 slot0
    selector = get_selector("slot0()")
    result = client.query_contract(sender, pool_addr, selector)
    
    if result:
        print(f"  - slot0 原始数据: {result}")
        try:
            clean = result.strip().lower().replace("0x", "")
            if len(clean) >= 64:
                sqrt_price_x96 = int(clean[:64], 16)
                print(f"  ✓ sqrtPriceX96: {sqrt_price_x96}")
                
                # 计算实际价格
                price = (sqrt_price_x96 / (2 ** 96)) ** 2
                print(f"  ✓ 当前价格: {price:.6f}")
        except Exception as e:
            print(f"  ✗ 解析失败: {e}")
    else:
        print("  ✗ 查询失败")
    
    # 查询 liquidity
    selector = get_selector("liquidity()")
    result = client.query_contract(sender, pool_addr, selector)
    
    if result:
        try:
            clean = result.strip().lower().replace("0x", "")
            if len(clean) >= 64:
                liquidity = int(clean[-64:], 16)
                print(f"  ✓ 当前流动性: {liquidity}")
        except Exception as e:
            print(f"  ✗ 解析失败: {e}")


# ==========================================
# 主测试流程
# ==========================================
if __name__ == "__main__":
    print("=" * 60)
    print("Uniswap V3 Core (0.8.20) 部署测试")
    print("=" * 60)
    
    # 配置
    SHARDORA_HOST = os.getenv("SHARDORA_HOST", "35.197.170.240")
    SHARDORA_PORT = int(os.getenv("SHARDORA_PORT", "23001"))
    PRIVATE_KEY = os.getenv("PRIVATE_KEY", "4b6525236a2029ab54e2c6162c483133c1af7d38bd960f85b1f485c31e696b7b")
    
    # 测试代币地址（需要提前部署）
    TOKEN0_ADDR = os.getenv("TOKEN0_ADDR", "").strip().lower().replace("0x", "")
    TOKEN1_ADDR = os.getenv("TOKEN1_ADDR", "").strip().lower().replace("0x", "")
    
    # 创建客户端
    client = ShardoraClient(SHARDORA_HOST, SHARDORA_PORT)
    sender = client.get_address(PRIVATE_KEY)
    print(f"\n[Info] 部署者地址: {sender}")
    print(f"[Info] 余额: {client.get_balance(sender)}")
    
    # 1. 编译合约
    compiled = compile_v3_contracts_0_8()
    if not compiled:
        print("\n[Error] 合约编译失败，退出")
        exit(1)
    
    # 2. 部署 Factory
    factory_addr, factory_abi = deploy_v3_factory(client, PRIVATE_KEY, compiled)
    if not factory_addr:
        print("\n[Error] Factory 部署失败，退出")
        exit(1)
    
    # 3. 启用费率等级
    enable_fee_amounts(client, PRIVATE_KEY, factory_addr)
    
    # 4. 如果提供了代币地址，创建交易池
    if TOKEN0_ADDR and TOKEN1_ADDR and len(TOKEN0_ADDR) == 40 and len(TOKEN1_ADDR) == 40:
        pool_addr = create_pool(client, PRIVATE_KEY, factory_addr, TOKEN0_ADDR, TOKEN1_ADDR, fee=3000)
        
        if pool_addr:
            # 5. 初始化池价格
            initialize_pool(client, PRIVATE_KEY, pool_addr, initial_price=1.0)
            
            # 6. 查询池状态
            query_pool_state(client, sender, pool_addr)
    else:
        print("\n[Info] 未提供代币地址，跳过创建池")
        print("[Info] 使用方法:")
        print("  export TOKEN0_ADDR=<token0_address>")
        print("  export TOKEN1_ADDR=<token1_address>")
        print("  python v3_deploy_test_0.8.py")
    
    print("\n" + "=" * 60)
    print("部署测试完成 (Solidity 0.8.20)")
    print("=" * 60)
    print(f"\n[Summary]")
    print(f"  Solidity 版本: 0.8.20")
    print(f"  Factory 地址: {factory_addr if factory_addr else 'N/A'}")
    if TOKEN0_ADDR and TOKEN1_ADDR:
        print(f"  Pool 地址: {pool_addr if 'pool_addr' in locals() else 'N/A'}")
