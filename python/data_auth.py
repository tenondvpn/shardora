import struct
import requests
import hashlib
import json
import time
from Crypto.Hash import keccak
from eth_abi import encode, decode
from ecdsa import SigningKey, SECP256k1
from ecdsa.util import sigencode_string_canonize

class DataMarketFullClient:
    def __init__(self, host, port, contract_addr, priv_key):
        self.base_url = f"http://{host}:{port}"
        self.base_url = f"http://{host}:{port}"
        self.tx_url = f"{self.base_url}/transaction"
        self.query_url = f"{self.base_url}/query_account"
        self.contract_addr = contract_addr.replace('0x', '').lower()
        self.priv_key = priv_key

        # 预计算身份
        self.sk = SigningKey.from_string(bytes.fromhex(priv_key), curve=SECP256k1)
        self.pubkey_hex = self.sk.verifying_key.to_string("uncompressed").hex()
        addr_hash = keccak.new(digest_bits=256, data=self.sk.verifying_key.to_string("raw")).digest()
        self.my_address = addr_hash[-20:].hex()

    def _json_handler(self, obj):
        if isinstance(obj, bytes): return "0x" + obj.hex()
        return obj

    def _send_call(self, func_name, params, p_types, o_types):
        """处理 View 查询 (abi_query_contract)"""
        selector = keccak.new(digest_bits=256, data=f"{func_name}({','.join(p_types)})".encode()).digest()[:4].hex()
        encoded = selector + encode(p_types, params).hex()

        resp = requests.post(f"{self.base_url}/abi_query_contract", data={
            "input": encoded, "address": self.contract_addr, "from": self.my_address
        })
        raw_hex = resp.text.strip().replace("0x", "")
        return decode(o_types, bytes.fromhex(raw_hex))

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

    def _uint64_to_bytes(self, val):
        return struct.pack('<Q', val)

    def _hex_to_bytes(self, hex_str):
        return bytes.fromhex(hex_str.replace('0x', ''))

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

    def _derive_address_from_pubkey(self, priv_bytes):
        sk = SigningKey.from_string(priv_bytes, curve=SECP256k1)
        pub_raw = sk.verifying_key.to_string("uncompressed")[1:]
        k = keccak.new(digest_bits=256).update(pub_raw)
        return k.digest()[-20:].hex()

    def send_contract_call(self, func_name, params, p_types):
        selector = keccak.new(digest_bits=256, data=f"{func_name}({','.join(p_types)})".encode()).digest()[:4].hex()
        input_hex = selector + encode(p_types, params).hex()
        # 获取 Nonce
        nr = requests.post(f"{self.base_url}/query_account", data={"address": self.my_address}).json()
        """发送合约调用交易"""
        # 1. 密钥处理
        sk = SigningKey.from_string(bytes.fromhex(self.priv_key), curve=SECP256k1)
        pubkey_full = sk.verifying_key.to_string("uncompressed")
        pubkey_hex = pubkey_full.hex()
        my_addr = self.contract_addr + self._derive_address_from_pubkey(bytes.fromhex(self.priv_key))

        # 2. 获取 Nonce
        acc_info = self.get_account_info(my_addr)
        nonce = (int(acc_info.get("nonce", 0)) if acc_info else 0) + 1

        # 3. 签名
        tx_hash = self.compute_hash(nonce, pubkey_hex, self.contract_addr, 0, 500000, 1, 8, input_hex)
        signature = sk.sign_digest_deterministic(tx_hash, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)

        # 4. 构造参数
        data = {
            "nonce": str(nonce),
            "pubkey": pubkey_hex,
            "to": self.contract_addr,
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

    # --- 1. View 接口 ---

    def getDataCount(self):
        res = self._send_call("getDataCount", [], [], ["uint256"])
        return res[0]

    def getPrice(self, data_id_bytes):
        res = self._send_call("getPrice", [data_id_bytes], ["bytes32"], ["uint256"])
        return res[0]

    def getHistory(self, data_id_bytes):
        struct_fmt = "(bytes32,bytes32,address,uint256,string)[]"
        res = self._send_call("getHistory", [data_id_bytes], ["bytes32"], [struct_fmt])
        fields = ["dataHash", "previousHash", "owner", "timestamp", "metadata"]
        return self.hexify([dict(zip(fields, item)) for item in res[0]])

    def getAllLatestRecords(self, offset, limit):
        struct_fmt = "(bytes32,bytes32,address,uint256,string)[]"
        res = self._send_call("getAllLatestRecords", [offset, limit], ["uint256", "uint256"], [struct_fmt])
        fields = ["dataHash", "previousHash", "owner", "timestamp", "metadata"]
        return self.hexify([dict(zip(fields, item)) for item in res[0]])

    def dataOwner(self, data_id_bytes):
        res = self._send_call("dataOwner", [data_id_bytes], ["bytes32"], ["address"])
        return res[0]

    # --- 2. Transaction 接口 ---

    def createData(self, data_id_bytes, metadata_str, price_uint):
        return self.send_contract_call("createData", [data_id_bytes, metadata_str, price_uint], ["bytes32", "string", "uint256"])

    def updateData(self, data_id_bytes, new_metadata_str):
        return self.send_contract_call("updateData", [data_id_bytes, new_metadata_str], ["bytes32", "string"])

    def setPrice(self, data_id_bytes, new_price_uint):
        return self.send_contract_call("setPrice", [data_id_bytes, new_price_uint], ["bytes32", "uint256"])

    def buyData(self, data_id_bytes, trade_note_str, pay_amount):
        # buyData 是 payable，需要传入 value
        return self.send_contract_call("buyData", [data_id_bytes, trade_note_str], ["bytes32", "string"], value=pay_amount)

    def hexify(self, data):
        """递归将数据结构中所有的 bytes 转换为 hex 字符串"""
        if isinstance(data, bytes):
            return "0x" + data.hex()
        if isinstance(data, list):
            return [self.hexify(item) for item in data]
        if isinstance(data, dict):
            return {k: self.hexify(v) for k, v in data.items()}
        return data

# --- 演示执行 ---
if __name__ == "__main__":
    conf = {
        "host": "35.197.170.240", "port": 23001,
        "contract": "f5c35074b7583ec006300e9f47d93cb45c486309",
        "priv": "c75f8d9b2a6bc0fe68eac7fef67c6b6f7c4f85163d58829b59110ff9e9210848"
    }

    client = DataMarketFullClient(conf["host"], conf["port"], conf["contract"], conf["priv"])
    test_id = hashlib.sha256(b"my_unique_data_id_2").digest()
    print(f"1. 正在发起创建交易...")
    tx_info = client.createData(test_id, "my_unique_data_id_2", 1)
    print(f"交易返回结果 (原始信息): {tx_info}")
    time.sleep(3)
    print(f"\n3. 正在查询存证历史 (需ABI解码)...")
    d_id = test_id
    print("total_count: ", client.getDataCount())
    print("current_price: ", client.getPrice(d_id))
    print("owner: ", client.dataOwner(d_id))
    print("latest: ", client.getAllLatestRecords(0, 100))
    print("history: ", client.getHistory(d_id))
