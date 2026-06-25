import sys
import time
import struct
import requests
import hashlib
import json
from Crypto.Hash import keccak
from eth_abi import encode, decode
from ecdsa import SigningKey, SECP256k1
from ecdsa.util import sigencode_string_canonize

class DataMarketFullClient:
    def __init__(self, host, port, contract_addr, priv_key):
        self.base_url = f"http://{host}:{port}"
        self.tx_url = f"{self.base_url}/transaction"
        self.query_url = f"{self.base_url}/query_account"
        self.contract_addr = contract_addr.replace('0x', '').lower()
        self.priv_key = priv_key
        
        self.sk = SigningKey.from_string(bytes.fromhex(priv_key), curve=SECP256k1)
        self.pubkey_uncompressed = self.sk.verifying_key.to_string("uncompressed")
        addr_hash = keccak.new(digest_bits=256, data=self.sk.verifying_key.to_string("raw")).digest()
        self.my_address = addr_hash[-20:].hex()

    def generateId(self, seed_str):
        """修复报错：实现 ID 生成逻辑"""
        return "0x" + hashlib.sha256(seed_str.encode()).hexdigest()

    def _uint64_to_bytes(self, val):
        return struct.pack('<Q', val)

    def _hex_to_bytes(self, hex_str):
        if hex_str.startswith('0x'):
            hex_str = hex_str[2:]
        return bytes.fromhex(hex_str)

    def get_latest_nonce(self, address_hex):
        """
        Query account info and get the latest Nonce
        """
        print(f"[Client] Querying nonce for address: {address_hex}")
        try:
            # Construct request, server QueryAccount expects 'address' parameter
            data = {"address": address_hex}
            resp = requests.post(self.query_url, data=data, timeout=5)

            if resp.status_code != 200:
                print(f"[Error] Query failed: {resp.text}")
                return 0 # Account might not exist, default to 0

            # Parse returned JSON
            # Server response example: {"address": "...", "balance": 1000, "nonce": 5, ...}
            # Note: Protobuf JSON fields might be omitted if they are default values, need handling
            try:
                account_info = resp.json()
                # If empty object or no nonce field, it's a new account, return 0
                nonce = int(account_info.get("nonce", 0))
                print(f"[Client] Current Nonce on chain: {nonce}")
                return nonce
            except json.JSONDecodeError:
                print(f"[Client] Failed to parse JSON, assuming new account. Resp: {resp.text}")
                return 0

        except Exception as e:
            print(f"[Error] Get nonce error: {e}")

    def _send_call(self, func_name, params, p_types, o_types):
        selector = keccak.new(digest_bits=256, data=f"{func_name}({','.join(p_types)})".encode()).digest()[:4].hex()
        encoded = selector + encode(p_types, params).hex()
        resp = requests.post(f"{self.base_url}/abi_query_contract", data={
            "input": encoded, "address": self.contract_addr, "from": self.my_address
        })
        raw_hex = resp.text.strip().replace("0x", "")
        if not raw_hex or raw_hex == "0" * 64: return [None] * len(o_types)
        return decode(o_types, bytes.fromhex(raw_hex))

    def send_contract_call(self, func_name, params, p_types, value=0):
        selector = keccak.new(digest_bits=256, data=f"{func_name}({','.join(p_types)})".encode()).digest()[:4].hex()
        input_hex = selector + encode(p_types, params).hex()
        addr = self.contract_addr + self.my_address
        nonce = self.get_latest_nonce(addr) + 1
        msg = bytearray()
        msg.extend(struct.pack('<Q', nonce))
        msg.extend(self.pubkey_uncompressed)
        msg.extend(bytes.fromhex(self.contract_addr))
        msg.extend(struct.pack('<Q', int(value)))
        msg.extend(struct.pack('<Q', 500000))
        msg.extend(struct.pack('<Q', 1))
        msg.extend(struct.pack('<Q', 8))
        msg.extend(bytes.fromhex(input_hex))
        
        tx_hash = keccak.new(digest_bits=256, data=msg).digest()
        signature = self.sk.sign_digest_deterministic(tx_hash, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
        
        data = {
            "nonce": str(nonce), "pubkey": self.pubkey_uncompressed.hex(), "to": self.contract_addr,
            "amount": str(value), "gas_limit": "500000", "gas_price": "1",
            "type": "8", "shard_id": "3", "input": input_hex,
            "sign_r": signature[0:32].hex(), "sign_s": signature[32:64].hex(), "sign_v": "0"
        }

        resp = requests.post(self.tx_url, data=data)
        if "verify signature failed" in resp.text:
            data["sign_v"] = "1"
            resp = requests.post(self.tx_url, data=data)
        
        try_times = 0
        while try_times < 15:
            new_nonce = self.get_latest_nonce(addr)
            if new_nonce >= nonce: 
                return True
            
            time.sleep(1)
            try_times += 1

        return False

    # --- 读接口 ---
    def getDataCount(self): return self._send_call("getDataCount", [], [], ["uint256"])[0]
    def getPrice(self, d_id): return self._send_call("getPrice", [d_id], ["bytes32"], ["uint256"])[0]
    def dataOwner(self, d_id): return self._send_call("dataOwner", [d_id], ["bytes32"], ["address"])[0]
    
    def getHistory(self, d_id):
        res = self._send_call("getHistory", [d_id], ["bytes32"], ["(bytes32,bytes32,address,uint256,string)[]"])
        fields = ["dataHash", "prevHash", "owner", "timestamp", "metadata"]
        return self.hexify([dict(zip(fields, item)) for item in res[0]]) if res[0] else []

    def getAllLatestRecords(self, offset, limit):
        res = self._send_call("getAllLatestRecords", [offset, limit], ["uint256", "uint256"], ["(bytes32,bytes32,address,uint256,string)[]"])
        fields = ["dataHash", "prevHash", "owner", "timestamp", "metadata"]
        return self.hexify([dict(zip(fields, item)) for item in res[0]]) if res[0] else []

    # --- 写接口 ---
    def createData(self, d_id, meta, price): return self.send_contract_call("createData", [d_id, meta, price], ["bytes32", "string", "uint256"])
    def updateData(self, d_id, meta): return self.send_contract_call("updateData", [d_id, meta], ["bytes32", "string"])
    def setPrice(self, d_id, price): return self.send_contract_call("setPrice", [d_id, price], ["bytes32", "uint256"])
    def buyData(self, d_id, note, pay): return self.send_contract_call("buyData", [d_id, note], ["bytes32", "string"], value=pay)

    def hexify(self, data):
        if isinstance(data, bytes): return "0x" + data.hex()
        if isinstance(data, list): return [self.hexify(item) for item in data]
        if isinstance(data, dict): return {k: self.hexify(v) for k, v in data.items()}
        return data
