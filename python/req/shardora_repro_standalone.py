#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import time
from dataclasses import dataclass

import requests
import solcx
from Crypto.Hash import keccak
from ecdsa import SECP256k1, SigningKey
from ecdsa.util import sigencode_string_canonize
from eth_abi import encode as eth_abi_encode
from eth_utils import to_checksum_address
from enum import IntEnum

SOLC_VERSION = "0.8.30"

class MessageHandleStatus(IntEnum):
    kConsensusSuccess = 0
    kMessageHandle = 10001
    kMessageHandleError = 10002
    kTxAccept = 10003
    kTxInvalidSignature = 10004
    kTxInvalidAddress = 10005
    kTxPoolFullReject = 10006
    kTxUserNonceInvalid = 10007
    kUnkonwn = 10008
    kRequestInvalid = 10009

    # 通用执行失败
    EVMC_FAILURE = 1

    # 执行被 REVERT 指令终止
    # 此时剩余 Gas 可能非零，且可能提供输出数据
    EVMC_REVERT = 2

    # Gas 耗尽
    EVMC_OUT_OF_GAS = 3

    # 触发了 INVALID 指令 (EIP-141 定义的 0xfe)
    EVMC_INVALID_INSTRUCTION = 4

    # 遇到未定义的指令
    EVMC_UNDEFINED_INSTRUCTION = 5

    # 栈溢出 (超过 1024 限制)
    EVMC_STACK_OVERFLOW = 6

    # 栈下溢
    EVMC_STACK_UNDERFLOW = 7

    # 无效的跳转目标 (JUMPDEST 限制)
    EVMC_BAD_JUMP_DESTINATION = 8

    # 越界内存访问 (例如 RETURNDATACOPY 读取范围超过缓冲区)
    EVMC_INVALID_MEMORY_ACCESS = 9

    # 调用深度超过限制 (通常是 1024)
    EVMC_CALL_DEPTH_EXCEEDED = 10

    # 静态模式违规 (在 STATICCALL 中尝试修改状态)
    EVMC_STATIC_MODE_VIOLATION = 11

    # 预编译合约或系统合约执行失败
    EVMC_PRECOMPILE_FAILURE = 12

    # 合约校验失败 (针对 ewasm 或 EVM 1.5 规则)
    EVMC_CONTRACT_VALIDATION_FAILURE = 13

    # 方法参数超出接受范围
    EVMC_ARGUMENT_OUT_OF_RANGE = 14

    # WebAssembly unreachable 指令被触发
    EVMC_WASM_UNREACHABLE_INSTRUCTION = 15

    # WebAssembly trap (例如除零错误、校验错误等)
    EVMC_WASM_TRAP = 16

    # 余额不足以支持转账 (Insufficient funds)
    EVMC_INSUFFICIENT_BALANCE = 17

    # --- 内部错误及拒绝 (负值) ---

    # EVM 实现通用内部错误
    EVMC_INTERNAL_ERROR = -1

    # EVM 拒绝执行该代码或消息
    EVMC_REJECTED = -2

    # 内存分配失败 (VM 无法申请所需内存)
    EVMC_OUT_OF_MEMORY = -3


IN_PROGRESS = {MessageHandleStatus.kMessageHandle, MessageHandleStatus.kTxAccept}
PROBE_POOL_SOL = """// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

contract ProbePool {
    uint256 public reserveSHARDORA;
    uint256 public reserveUSDC;
    uint256 public totalSells;
    uint256 public lastOut;

    event PoolInitialized(uint256 reserveSHARDORA, uint256 reserveUSDC);
    event ShardoraSold(address indexed sender, uint256 amountIn, uint256 amountOut, uint256 newReserveSHARDORA, uint256 newReserveUSDC);
    event TestEvent(uint256 num);

    constructor(uint256 _reserveSHARDORA, uint256 _reserveUSDC) payable {
        reserveSHARDORA = _reserveSHARDORA;
        reserveUSDC = _reserveUSDC;
        emit PoolInitialized(_reserveSHARDORA, _reserveUSDC);
    }

    function sellSHARDORA(uint256 minOut) external payable returns (uint256 out) {
        emit TestEvent(20000);
        require(msg.value > 0, "ProbePool: zero in");
        emit TestEvent(20001);
        require(reserveSHARDORA > 0 && reserveUSDC > 0, "ProbePool: empty");
        emit TestEvent(20002);
        
        out = (msg.value * reserveUSDC) / (reserveSHARDORA + msg.value);
        emit TestEvent(20003);
        require(out >= minOut, "ProbePool: slippage");
        emit TestEvent(20004);
        
        reserveSHARDORA += msg.value;
        reserveUSDC -= out;
        totalSells += 1;
        lastOut = out;
        emit TestEvent(20005);

        emit ShardoraSold(msg.sender, msg.value, out, reserveSHARDORA, reserveUSDC);
    }
}
"""

PROBE_TREASURY_SOL = """// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

contract ProbeTreasury {
    address public pool;
    address public bridge;
    uint256 public totalSwaps;
    uint256 public lastOut;

    event TreasuryInitialized(address pool);
    event BridgeSet(address bridge);
    event TestEvent(uint256 num);
    event SwapExecuted(address indexed sender, uint256 amountIn, uint256 amountOut);

    constructor(address _pool) payable { 
        pool = _pool; 
        emit TestEvent(50001);
        emit TreasuryInitialized(_pool);
    }

    function setBridge(address _bridge) external { 
        bridge = _bridge; 
        emit TestEvent(50003);
        emit BridgeSet(_bridge);
    }

    modifier onlyBridge() { 
        require(msg.sender == bridge, "ProbeTreasury: not bridge");
        _;
    }

    function swap(uint256 minOut) external payable onlyBridge returns (uint256 out) {
        (bool ok, bytes memory ret) = pool.call{value: msg.value}(
            abi.encodeWithSignature("sellSHARDORA(uint256)", minOut)
        );

        emit TestEvent(40000);
        require(ok, "ProbeTreasury: pool call failed");
        
        out = abi.decode(ret, (uint256));
        require(out > 0, "ProbeTreasury: zero out");
        
        totalSwaps += 1;
        lastOut = out;

        emit SwapExecuted(msg.sender, msg.value, out);
    }
}
"""

PROBE_BRIDGE_SOL = """// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

contract ProbeBridge {
    address public treasury;
    uint256 public totalRequests;
    uint256 public lastOut;

    event BridgeInitialized(address treasury);
    event RequestReceived(address indexed sender, uint256 amountIn, uint256 minOut);
    event RequestCompleted(address indexed sender, uint256 amountOut);
    event TestEvent(uint256 num);

    constructor(address _treasury) { 
        treasury = _treasury; 
        emit TestEvent(50002);
        emit BridgeInitialized(_treasury);
    }

    function request(uint256 minOut) external payable returns (uint256 out) {
        require(msg.value > 0, "ProbeBridge: zero value");

        (bool ok, bytes memory ret) = treasury.call{value: msg.value}(
            abi.encodeWithSignature("swap(uint256)", minOut)
        );
        emit TestEvent(10002);
        require(ok, "ProbeBridge: treasury call failed");
        emit TestEvent(10003);
        
        out = abi.decode(ret, (uint256));
        require(out > 0, "ProbeBridge: zero out");
        emit TestEvent(10005);
        
        totalRequests += 1;
        lastOut = out;

        emit RequestCompleted(msg.sender, out);
    }
}
"""


def normalize_status(status):
    if status is None:
        return None
    return MessageHandleStatus(status)

def selector(signature: str) -> str:
    h = keccak.new(digest_bits=256)
    h.update(signature.encode("utf-8"))
    return h.digest()[:4].hex()


def encode_call(signature: str, types: list, args: list) -> str:
    return selector(signature) + eth_abi_encode(types, args).hex()

def parse_hex_output(raw: str | None) -> str:
    if not raw:
        return ""
    t = raw.strip()
    try:
        j = json.loads(t)
        for k in ("output", "result", "data", "value"):
            v = j.get(k)
            if isinstance(v, str):
                t = v
                break
    except Exception:
        pass
    h = t.lower().replace("0x", "")
    if len(h) % 2 != 0:
        return ""
    if all(c in "0123456789abcdef" for c in h):
        return h
    return ""


def u256(out_hex: str, word: int = 0) -> int | None:
    off = word * 64
    if not out_hex or len(out_hex) < off + 64:
        return None
    return int(out_hex[off:off + 64], 16)


def create2(sender_hex: str, salt_hex: str, bytecode_hex: str) -> str:
    prefix = bytes.fromhex("ff")
    sender = bytes.fromhex(sender_hex.replace("0x", ""))
    salt = bytes.fromhex(salt_hex.replace("0x", "").zfill(64))
    code = bytes.fromhex(bytecode_hex.replace("0x", ""))
    kh = keccak.new(digest_bits=256)
    kh.update(code)
    code_hash = kh.digest()
    kf = keccak.new(digest_bits=256)
    kf.update(prefix + sender + salt + code_hash)
    return kf.digest()[-20:].hex()


def install_solc():
    try:
        solcx.install_solc(SOLC_VERSION)
    except Exception:
        pass
    solcx.set_solc_version(SOLC_VERSION)


def compile_inline(source: str, name: str) -> str:
    install_solc()
    try:
        compiled = solcx.compile_source(
            source,
            output_values=["abi", "bin"],
            evm_version="shanghai",
            optimize=True,
            optimize_runs=200,
            via_ir=True,
        )
    except Exception:
        compiled = solcx.compile_source(
            source,
            output_values=["abi", "bin"],
            evm_version="shanghai",
            optimize=True,
            optimize_runs=200,
            via_ir=False,
        )
    for k, v in compiled.items():
        if k.endswith(":" + name) and v.get("bin"):
            return v["bin"]
    for _k, v in compiled.items():
        if v.get("bin"):
            return v["bin"]
    raise RuntimeError(f"compile {name} failed")


@dataclass
class ShardoraClient:
    host: str
    port: int

    @property
    def tx_url(self): return f"http://{self.host}:{self.port}/transaction"
    @property
    def query_url(self): return f"http://{self.host}:{self.port}/query_account"
    @property
    def receipt_url(self): return f"http://{self.host}:{self.port}/transaction_receipt"
    @property
    def query_contract_url(self): return f"http://{self.host}:{self.port}/query_contract"

    @staticmethod
    def _u64(v: int) -> bytes: return struct.pack("<Q", int(v))
    @staticmethod
    def _hb(h: str) -> bytes: return bytes.fromhex(h[2:] if h.startswith("0x") else h)

    def addr_from_pk(self, pk_hex: str) -> str:
        pk = pk_hex[2:] if pk_hex.startswith("0x") else pk_hex
        sk = SigningKey.from_string(bytes.fromhex(pk), curve=SECP256k1)
        pub = sk.verifying_key.to_string("uncompressed")[1:]
        h = keccak.new(digest_bits=256); h.update(pub)
        return h.digest()[-20:].hex()

    def balance(self, addr_hex: str) -> int:
        a = addr_hex.replace("0x", "").lower()
        try:
            r = requests.post(self.query_url, data={"address": a}, timeout=10)
            if r.status_code == 200:
                return int(r.json().get("balance", 0))
        except Exception:
            pass
        return 0

    def nonce(self, addr_hex: str) -> int:
        a = addr_hex.replace("0x", "").lower()
        try:
            r = requests.post(self.query_url, data={"address": a}, timeout=10)
            if r.status_code == 200:
                return int(r.json().get("nonce", 0))
        except Exception:
            pass
        return 0

    def compute_hash(self, *, nonce, pubkey, to, amount, gas_limit, gas_price, step, contract_code="", input_hex="", prefund=0):
        msg = bytearray()
        msg.extend(self._u64(nonce))
        msg.extend(self._hb(pubkey))
        msg.extend(self._hb(to))
        msg.extend(self._u64(amount))
        msg.extend(self._u64(gas_limit))
        msg.extend(self._u64(gas_price))
        msg.extend(self._u64(step))
        if contract_code:
            msg.extend(self._hb(contract_code))
        if input_hex:
            msg.extend(self._hb(input_hex))
        if prefund > 0:
            msg.extend(self._u64(prefund))
        h = keccak.new(digest_bits=256); h.update(msg)
        return h.digest()

    def send_tx(self, pk_hex: str, to_hex: str, *, amount=0, gas_limit=5_000_000, gas_price=1, step=0, contract_code="", input_hex="", prefund=0):
        pk = pk_hex[2:] if pk_hex.startswith("0x") else pk_hex
        sk = SigningKey.from_string(bytes.fromhex(pk), curve=SECP256k1)
        pub = sk.verifying_key.to_string("uncompressed").hex()
        my = self.addr_from_pk(pk)
        nonce_addr = (to_hex + my) if step == 8 else my
        n = self.nonce(nonce_addr) + 1
        txh = self.compute_hash(
            nonce=n, pubkey=pub, to=to_hex, amount=amount,
            gas_limit=gas_limit, gas_price=gas_price, step=step,
            contract_code=contract_code, input_hex=input_hex, prefund=prefund
        )
        sig = sk.sign_digest_deterministic(txh, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
        data = {
            "nonce": str(n), "pubkey": pub, "to": to_hex, "amount": str(amount),
            "gas_limit": str(gas_limit), "gas_price": str(gas_price), "shard_id": "0", "type": str(step),
            "sign_r": sig[0:32].hex(), "sign_s": sig[32:64].hex(), "sign_v": "0",
        }
        if contract_code:
            data["bytes_code"] = contract_code
        if input_hex:
            data["input"] = input_hex
        if prefund > 0:
            data["prefund"] = str(prefund)
        r = requests.post(self.tx_url, data=data, timeout=60)
        txt = (r.text or "")[:200]
        print("  tx response:", txt)
        return txh.hex()

    def wait_receipt(self, tx_hash: str, timeout=300):
        start = time.time()
        while time.time() - start < timeout:
            try:
                r = requests.post(self.receipt_url, data={"tx_hash": tx_hash}, timeout=10)
                if r.status_code == 200:
                    j = r.json()
                    st = normalize_status(j.get("status"))
                    if st is not None and st not in IN_PROGRESS:
                        return True, st, j
            except Exception:
                pass
            time.sleep(1)
        return False, None, {}

    def q_contract(self, from_hex: str, addr_hex: str, input_hex: str):
        r = requests.post(
            self.query_contract_url,
            data={"from": from_hex.replace("0x", "").lower(), "address": addr_hex.replace("0x", "").lower(), "input": input_hex},
            timeout=20,
        )
        return r.text if r.status_code == 200 else None


def deploy(client: ShardoraClient, pk: str, deployer: str, code: str, salt: str, label: str, amount: int = 0):
    target = create2(deployer, salt, code)
    print(f"[Deploy] {label} -> {target}")
    tx = client.send_tx(pk, target, step=6, contract_code=code, prefund=10_000_000, gas_limit=5_000_000, amount=amount)
    ok, st, _ = client.wait_receipt(tx, timeout=600)
    print(f"  receipt={ok} status={st} tx={tx}")
    return target


def q_u(client: ShardoraClient, from_hex: str, addr_hex: str, sig: str, types=None, args=None):
    types = types or []
    args = args or []
    raw = client.q_contract(from_hex, addr_hex, encode_call(sig, types, args))
    return u256(parse_hex_output(raw))


def run_business(client: ShardoraClient, pk: str, sender: str, bridge: str, pool: str, amount_shardora: int):
    print("\n=== Business Repro: requestWithdrawToSolanaFromSHARDORA ===")
    bt = q_u(client, sender, bridge, "totalWithdrawRequests()")
    brs = q_u(client, sender, pool, "reserveSHARDORA()")
    bru = q_u(client, sender, pool, "reservesUSDC()")
    print("before totalWithdrawRequests=", bt, "pool=", brs, bru)
    tx = client.send_tx(
        pk, bridge, amount=amount_shardora, gas_limit=500000000, gas_price=1, step=8,
        input_hex=encode_call("requestWithdrawToSolanaFromSHARDORA(bytes32,uint256)", ["bytes32", "uint256"], [bytes.fromhex("aa"*32), 1])
    )
    ok, st, raw = client.wait_receipt(tx, timeout=300)
    print("tx=", tx, "receipt_ok=", ok, "status=", st, "raw=", raw)
    at = q_u(client, sender, bridge, "totalWithdrawRequests()")
    ars = q_u(client, sender, pool, "reserveSHARDORA()")
    aru = q_u(client, sender, pool, "reservesUSDC()")
    print("after  totalWithdrawRequests=", at, "pool=", ars, aru)


def run_probe_chain(client: ShardoraClient, pk: str, sender: str):
    print("\n=== Probe Repro: Bridge->Treasury->Pool ===")
    pool_bin = compile_inline(PROBE_POOL_SOL, "ProbePool")
    treasury_bin = compile_inline(PROBE_TREASURY_SOL, "ProbeTreasury")
    bridge_bin = compile_inline(PROBE_BRIDGE_SOL, "ProbeBridge")

    pool_ctor = eth_abi_encode(["uint256", "uint256"], [10_000, 10_000]).hex()
    pool = deploy(client, pk, sender, pool_bin + pool_ctor, "abf3", "ProbePool", amount=10000000)

    tr_ctor = eth_abi_encode(["address"], [to_checksum_address("0x" + pool)]).hex()
    treasury = deploy(client, pk, sender, treasury_bin + tr_ctor, "abf4", "ProbeTreasury", amount=10000000)

    br_ctor = eth_abi_encode(["address"], [to_checksum_address("0x" + treasury)]).hex()
    bridge = deploy(client, pk, sender, bridge_bin + br_ctor, "abf5", "ProbeBridge")

    tx_set = client.send_tx(pk, treasury, step=8, gas_limit=8_000_000, input_hex=encode_call("setBridge(address)", ["address"], [to_checksum_address("0x" + bridge)]))
    ok_set, st_set, _ = client.wait_receipt(tx_set, timeout=300)
    print("setBridge tx=", tx_set, "receipt=", ok_set, st_set)

    b_rs = q_u(client, sender, pool, "reserveSHARDORA()")
    b_ru = q_u(client, sender, pool, "reserveUSDC()")
    b_req = q_u(client, sender, bridge, "totalRequests()")
    b_sw = q_u(client, sender, treasury, "totalSwaps()")
    print("before pool=", b_rs, b_ru, "bridgeReq=", b_req, "treasurySwaps=", b_sw)

    tx = client.send_tx(pk, bridge, amount=2, gas_limit=8_000_000, step=8, input_hex=encode_call("request(uint256)", ["uint256"], [1]))
    ok, st, raw = client.wait_receipt(tx, timeout=300)
    print("request tx=", tx, "receipt=", ok, st, "raw=", raw)

    a_rs = q_u(client, sender, pool, "reserveSHARDORA()")
    a_ru = q_u(client, sender, pool, "reserveUSDC()")
    a_req = q_u(client, sender, bridge, "totalRequests()")
    a_sw = q_u(client, sender, treasury, "totalSwaps()")
    print("after  pool=", a_rs, a_ru, "bridgeReq=", a_req, "treasurySwaps=", a_sw)
    run_business(client, pk, sender, bridge, pool, 100)

def main():
    ap = argparse.ArgumentParser(description="Standalone Shardora repro script (no project imports)")
    ap.add_argument("--host", default="35.197.170.240")
    ap.add_argument("--port", type=int, default=23001)
    ap.add_argument("--private-key", required=True, help="0x... private key used as sender")
    ap.add_argument("--bridge", default="", help="Business repro: ShardoraBridge address")
    ap.add_argument("--pool", default="", help="Business repro: PoolB address")
    ap.add_argument("--amount-shardora", type=int, default=100, help="Business repro amount for requestWithdrawToSolanaFromSHARDORA")
    ap.add_argument("--skip-business", action="store_true")
    ap.add_argument("--skip-probe", action="store_true")
    args = ap.parse_args()

    pk = args.private_key.strip()
    if not pk.startswith("0x"):
        pk = "0x" + pk

    client = ShardoraClient(args.host, args.port)
    sender = client.addr_from_pk(pk)
    print("sender=0x" + sender, "balance=", client.balance(sender))

    if not args.skip_probe:
        run_probe_chain(client, pk, sender)


if __name__ == "__main__":
    main()

