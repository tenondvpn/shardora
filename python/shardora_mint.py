import struct
import requests
import hashlib
import json
import secrets
import time
from Crypto.Hash import keccak
from ecdsa import SigningKey, SECP256k1, VerifyingKey
from ecdsa.util import sigencode_string_canonize
import sha3 # 对应你代码中的 sha3.keccak_256
from eth_abi import encode # 用于标准的 ABI 编码
from eth_utils import decode_hex, encode_hex

# ==========================================
# 1. 核心工具类
# ==========================================

class ShardoraClient:
    def __init__(self, host, port):
        self.base_url = f"http://{host}:{port}"
        self.tx_url = f"{self.base_url}/transaction"
        self.query_url = f"{self.base_url}/query_account"

    def _uint64_to_bytes(self, val):
        return struct.pack('<Q', val)

    def _hex_to_bytes(self, hex_str):
        return bytes.fromhex(hex_str.replace('0x', ''))

    def _derive_address_from_pubkey(self, priv_bytes):
        sk = SigningKey.from_string(priv_bytes, curve=SECP256k1)
        pub_raw = sk.verifying_key.to_string("uncompressed")[1:]
        k = keccak.new(digest_bits=256).update(pub_raw)
        return k.digest()[-20:].hex()

    def get_account_info(self, address_hex):
        """查询账户信息"""
        try:
            data = {"address": address_hex.replace('0x', '')}
            resp = requests.post(self.query_url, data=data, timeout=5)
            if resp.status_code == 200:
                print (f"get account: {resp.json()}")
                return resp.json()
        except Exception as e:
            print(f"[Query Error] {e}")
        return None

    def compute_hash(self, nonce, pubkey_hex, to_hex, amount, gas_limit, gas_price, step, input_hex=''):
        """计算交易哈希 (RLP-like serialization for Shardora)"""
        msg = bytearray()
        msg.extend(self._uint64_to_bytes(nonce))
        msg.extend(self._hex_to_bytes(pubkey_hex))
        msg.extend(self._hex_to_bytes(to_hex))
        msg.extend(self._uint64_to_bytes(amount))
        msg.extend(self._uint64_to_bytes(gas_limit))
        msg.extend(self._uint64_to_bytes(gas_price))
        msg.extend(self._uint64_to_bytes(step))
        if input_hex:
            msg.extend(self._hex_to_bytes(input_hex))
        
        k = keccak.new(digest_bits=256)
        k.update(msg)
        return k.digest()

    def send_contract_call(self, priv_key_hex, contract_addr, input_hex):
        """发送合约调用交易"""
        # 1. 密钥处理
        sk = SigningKey.from_string(bytes.fromhex(priv_key_hex), curve=SECP256k1)
        pubkey_full = sk.verifying_key.to_string("uncompressed")
        pubkey_hex = pubkey_full.hex()
        my_addr = contract_addr + self._derive_address_from_pubkey(bytes.fromhex(priv_key_hex))

        # 2. 获取 Nonce
        acc_info = self.get_account_info(my_addr)
        nonce = (int(acc_info.get("nonce", 0)) if acc_info else 0) + 1
        
        # 3. 签名
        tx_hash = self.compute_hash(nonce, pubkey_hex, contract_addr, 0, 500000, 1, 8, input_hex)
        signature = sk.sign_digest_deterministic(tx_hash, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
        
        # 4. 构造参数
        data = {
            "nonce": str(nonce),
            "pubkey": pubkey_hex,
            "to": contract_addr,
            "amount": "0",
            "gas_limit": "500000",
            "gas_price": "1",
            "type": "8",
            "shard_id": str(3),
            "input": input_hex,
            "sign_r": signature[0:32].hex(),
            "sign_s": signature[32:64].hex(),
            "sign_v": "0"
        }

        print(f"[*] Dispatching transaction from {my_addr} to contract...")
        resp = requests.post(self.tx_url, data=data, timeout=5)
        
        # 自动重试 V=1
        if "verify signature failed" in resp.text:
            data["sign_v"] = "1"
            resp = requests.post(self.tx_url, data=data, timeout=5)
        
        return resp.status_code, resp.text

# ==========================================
# 2. 逻辑辅助函数
# ==========================================

def generate_random_account():
    """生成随机私钥及其对应的地址"""
    priv_bytes = bytes.fromhex("fb0e810c37300541a67c9f7d4c79a62697a154b8327b72dbf8d1e241955dff13")
    sk = SigningKey.from_string(priv_bytes, curve=SECP256k1)
    pub_raw = sk.verifying_key.to_string("uncompressed")[1:]
    k = keccak.new(digest_bits=256).update(pub_raw)
    return {"priv": priv_bytes.hex(), "addr": k.digest()[-20:].hex()}

def _keccak256_str(s: str) -> str:
    """对应你提供的代码片段"""
    k = sha3.keccak_256()
    k.update(bytes(s, 'utf-8'))
    return k.hexdigest()

def encode_contract_call(function: str, types_list: list, params_list: list) -> str:
    """
    生成合约调用的 Input 编码
    逻辑：keccak256(签名)[:8] + encode(参数)
    """
    return (_keccak256_str(f"{function}({','.join(types_list)})")[:8] + 
        encode_hex(encode(types_list, params_list))[2:])

def encode_abi_send_incentive(receiver_addr):
    """编码 sendIncentive(address) 调用数据"""
    # keccak256("sendIncentive(address)")[:4] -> 9b8d80f0
    input_data = encode_contract_call(
        "sendIncentive", 
        ["address"], 
        [receiver_addr]
    )
    return input_data

# ==========================================
# 3. 主程序：生成、执行、查询
# ==========================================

if __name__ == "__main__":
    # 配置信息
    HOST = "35.197.170.240"
    PORT = 23001
    CONTRACT_ADDR = "e464718ceba0a18a225fe28a963e3aab0071771c"
    OWNER_KEY = "c75f8d9b2a6bc0fe68eac7fef67c6b6f7c4f85163d58829b59110ff9e9210848"

    client = ShardoraClient(HOST, PORT)

    # 第一步：为新用户生成随机账户
    new_user = generate_random_account()
    print(f"[1] New Receiver Generated:\n    Addr: {new_user['addr']}\n    Priv: {new_user['priv']}")

    # 第二步：构造合约 Input
    abi_input = encode_abi_send_incentive(new_user['addr'])
    
    # 第三步：发起合约调用交易
    status, msg = client.send_contract_call(OWNER_KEY, CONTRACT_ADDR, abi_input)
    print(f"[2] Transaction Sent. Status: {status}, Msg: {msg}， abi_input: {abi_input}")

    if status == 200:
        print(f"[3] Waiting 5 seconds for block confirmation...")
        time.sleep(15) # 等待出块

        # 第四步：查询新账户余额验证结果
        print(f"[4] Verifying Balance for {new_user['addr']}...")
        result = client.get_account_info(new_user['addr'])
        
        if result:
            final_balance = int(result.get("balance", 0))
            print(f"    Current Balance: {final_balance}")
    else:
        print("\n❌ ERROR: Transaction failed to reach the mempool.")