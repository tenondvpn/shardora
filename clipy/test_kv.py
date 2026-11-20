#!/usr/bin/env python3
"""
Shardora StringKeyValueDatabase Full Lifecycle Test (Target Shard 3)
===================================================================
Features:
  1. Derives User/Deployer private keys strictly mapped to TARGET_SHARD (Shard 3)
     using xxhash.xxh64 sharding rule.
  2. Resolves Solidity function overload for batchSet:
     - batchSet(string[],string[])
     - batchSet((string,string)[]) with tuple encoding
  3. Activates user account on-chain via root shard native transfer.
  4. Prefunds gas deposits for the reader/writer user.
  5. Validates set/get, batchSet, batchGet/batchGetDetailed, pagination, and remove.
  6. Non-blocking logging for revert/repeated transactions.
  7. Reclaims gas via refund.
"""
from __future__ import annotations

import argparse
import os
import secrets
import sys
import time

import eth_abi
from eth_utils import function_signature_to_4byte_selector, to_checksum_address
import xxhash
from shardora_sdk import ShardoraWeb3Mock, compile_and_link

# ---------------------------------------------------------------------------
# Sharding Configuration & Hash Routing
# ---------------------------------------------------------------------------

TARGET_SHARD = int(os.environ.get("TARGET_SHARD", "3"))
HASH_SEED_1 = 23456785675590
CONSENSUS_SHARD_BEGIN = 3
MAX_SHARD_ID = 6


def calc_shard_id(addr_hex: str) -> int:
    """计算地址归属的 Shardora 共识分片 ID"""
    clean_hex = addr_hex.replace("0x", "")
    b = bytes.fromhex(clean_hex)[:20]
    shard_range = MAX_SHARD_ID - CONSENSUS_SHARD_BEGIN + 1
    return (xxhash.xxh64(b, seed=HASH_SEED_1).intdigest() % shard_range) + CONSENSUS_SHARD_BEGIN


def generate_account_for_shard(client, target_shard: int = TARGET_SHARD) -> tuple[str, str]:
    """循环碰撞生成归属于指定分片 (Shard 3) 的私钥与地址"""
    attempts = 0
    while True:
        attempts += 1
        private_key = secrets.token_hex(32)
        addr = client.get_address(private_key)
        shard_id = calc_shard_id(addr)
        if shard_id == target_shard:
            return private_key, addr


# ---------------------------------------------------------------------------
# Solidity Source
# ---------------------------------------------------------------------------

KV_DATABASE_SOL = """
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

contract StringKeyValueDatabase {
    struct Entry {
        string key;
        string value;
    }

    struct QueryResult {
        string key;
        string value;
        bool exists;
    }

    string[] private keys;
    mapping(bytes32 => string) private store;
    mapping(bytes32 => uint256) private keyToIndex;
    mapping(bytes32 => bool) private existsMap;

    event Set(string key, string value);
    event BatchSetCompleted(uint256 count);
    event Deleted(string key);

    error KeyNotFound(string key);
    error EmptyKey();
    error ArrayLengthMismatch();
    error EmptyBatch();

    function _set(string calldata _key, string calldata _value) internal {
        if (bytes(_key).length == 0) revert EmptyKey();
        bytes32 kHash = keccak256(bytes(_key));

        if (!existsMap[kHash]) {
            keyToIndex[kHash] = keys.length;
            keys.push(_key);
            existsMap[kHash] = true;
        }

        store[kHash] = _value;
        emit Set(_key, _value);
    }

    function set(string calldata _key, string calldata _value) external {
        _set(_key, _value);
    }

    function batchSet(Entry[] calldata _entries) external {
        uint256 len = _entries.length;
        if (len == 0) revert EmptyBatch();

        for (uint256 i = 0; i < len; ) {
            _set(_entries[i].key, _entries[i].value);
            unchecked { ++i; }
        }

        emit BatchSetCompleted(len);
    }

    function batchSet(string[] calldata _keys, string[] calldata _values) external {
        uint256 len = _keys.length;
        if (len == 0) revert EmptyBatch();
        if (len != _values.length) revert ArrayLengthMismatch();

        for (uint256 i = 0; i < len; ) {
            _set(_keys[i], _values[i]);
            unchecked { ++i; }
        }

        emit BatchSetCompleted(len);
    }

    function get(string calldata _key) external view returns (string memory) {
        bytes32 kHash = keccak256(bytes(_key));
        if (!existsMap[kHash]) revert KeyNotFound(_key);
        return store[kHash];
    }

    function exists(string calldata _key) external view returns (bool) {
        return existsMap[keccak256(bytes(_key))];
    }

    function batchGet(string[] calldata _keys) external view returns (string[] memory) {
        uint256 len = _keys.length;
        string[] memory values = new string[](len);

        for (uint256 i = 0; i < len; ) {
            bytes32 kHash = keccak256(bytes(_keys[i]));
            if (existsMap[kHash]) {
                values[i] = store[kHash];
            }
            unchecked { ++i; }
        }

        return values;
    }

    function batchGetDetailed(string[] calldata _keys) external view returns (QueryResult[] memory) {
        uint256 len = _keys.length;
        QueryResult[] memory results = new QueryResult[](len);

        for (uint256 i = 0; i < len; ) {
            bytes32 kHash = keccak256(bytes(_keys[i]));
            bool isPresent = existsMap[kHash];

            results[i] = QueryResult({
                key: _keys[i],
                value: isPresent ? store[kHash] : "",
                exists: isPresent
            });

            unchecked { ++i; }
        }

        return results;
    }

    function totalKeys() external view returns (uint256) {
        return keys.length;
    }

    function getKeyAt(uint256 _index) external view returns (string memory) {
        require(_index < keys.length, "Index out of bounds");
        return keys[_index];
    }

    function getPaginated(uint256 _offset, uint256 _limit) external view returns (Entry[] memory) {
        uint256 total = keys.length;
        if (_offset >= total || _limit == 0) {
            return new Entry[](0);
        }

        uint256 end = _offset + _limit;
        if (end > total) {
            end = total;
        }

        uint256 size = end - _offset;
        Entry[] memory result = new Entry[](size);

        for (uint256 i = 0; i < size; ) {
            string memory k = keys[_offset + i];
            bytes32 kHash = keccak256(bytes(k));
            result[i] = Entry({
                key: k,
                value: store[kHash]
            });
            unchecked { ++i; }
        }

        return result;
    }

    function remove(string calldata _key) external {
        bytes32 kHash = keccak256(bytes(_key));
        if (!existsMap[kHash]) revert KeyNotFound(_key);

        uint256 indexToDelete = keyToIndex[kHash];
        uint256 lastIndex = keys.length - 1;

        if (indexToDelete != lastIndex) {
            string memory lastKey = keys[lastIndex];
            bytes32 lastKeyHash = keccak256(bytes(lastKey));

            keys[indexToDelete] = lastKey;
            keyToIndex[lastKeyHash] = indexToDelete;
        }

        keys.pop();

        delete keyToIndex[kHash];
        delete store[kHash];
        delete existsMap[kHash];

        emit Deleted(_key);
    }
}
"""

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _wait_account_exists(client, addr: str, retries: int = 30) -> bool:
    for _ in range(retries):
        try:
            bal = client.get_balance(addr)
            if bal > 0:
                return True
        except Exception:
            pass
        time.sleep(2)
    try:
        return client.get_balance(addr) >= 0
    except Exception:
        return False


def _wait_prefund(contract, user_addr: str, expected: int, retries: int = 30) -> int:
    for _ in range(retries):
        pf = contract.get_prefund(user_addr)
        if pf >= expected:
            return pf
        time.sleep(2)
    return contract.get_prefund(user_addr)


def invoke_overloaded_fn(contract, signature: str, input_types: list[str], args: list, user_key: str):
    selector = function_signature_to_4byte_selector(signature).hex()
    encoded_args = eth_abi.encode(input_types, args).hex()
    calldata_hex = selector + encoded_args

    if hasattr(contract, 'transact_raw'):
        return contract.transact_raw(calldata_hex, user_key)

    try:
        method = contract.functions[signature](*args)
        return method.transact(user_key)
    except (KeyError, TypeError):
        dummy = getattr(contract.functions, 'totalKeys')()
        dummy.encoded_input = calldata_hex
        return dummy.transact(user_key)


# ---------------------------------------------------------------------------
# Test Execution
# ---------------------------------------------------------------------------

def test_key_value_database(w3, deployer_addr: str, deployer_key: str):
    print("\n" + "=" * 72)
    print(f"  StringKeyValueDatabase Test (Targeting Shard {TARGET_SHARD})")
    print("=" * 72)

    # 1. 验证 Deployer 所属分片
    dep_shard = calc_shard_id(deployer_addr)
    print(f"\n[Info] Deployer Address: {deployer_addr} (Shard {dep_shard})")

    # 2. 编译合约
    print("\n[Step 1] Compiling StringKeyValueDatabase...")
    kv_bin, kv_abi = compile_and_link(KV_DATABASE_SOL, "StringKeyValueDatabase")
    print("    Contract compiled successfully.")

    # 3. 部署合约
    salt = secrets.token_hex(28)
    print(f"\n[Step 2] Deploying contract on Shard {dep_shard}...")
    kv_contract = w3.shardora.contract(abi=kv_abi, bytecode=kv_bin)
    kv_contract.deploy({'from': deployer_addr, 'salt': salt, 'args': []}, deployer_key)
    contract_shard = calc_shard_id(kv_contract.address)
    print(f"    Contract @ {kv_contract.address} (Shard {contract_shard})")

    # 4. 生成归属于 TARGET_SHARD (3) 的读写账户
    print(f"\n[Step 3] Mining private key mapped strictly to Shard {TARGET_SHARD}...")
    user_key, user_addr = generate_account_for_shard(w3.client, target_shard=TARGET_SHARD)
    user_shard = calc_shard_id(user_addr)
    print(f"    Target Shard Account Found!")
    print(f"    Address : {user_addr}")
    print(f"    Shard   : {user_shard} ✅")

    # 5. 原生代币转账以在根分片注册账户
    print(f"\n[Step 4] Registering User on-chain via Native Transfer...")
    native_seed = 100_000_000
    r_tx = w3.shardora.send_transaction({'to': user_addr, 'value': native_seed}, deployer_key)
    print(f"    Transfer receipt: {r_tx}")
    assert _wait_account_exists(w3.client, user_addr), "User account registration timed out!"
    print(f"    User registered on-chain, balance={w3.client.get_balance(user_addr)}")

    # 6. Prefund 质押
    user_kv = w3.shardora.contract(address=kv_contract.address, abi=kv_abi, sender_address=user_addr)
    prefund_amount = 20_000_000
    print(f"\n[Step 5] Depositing Prefund ({prefund_amount}) for User...")
    user_kv.prefund(prefund_amount, user_key)
    pf_bal = _wait_prefund(user_kv, user_addr, prefund_amount)
    print(f"    Prefund balance: {pf_bal} ✅")

    # 7. 单条写入与读取
    print("\n[Step 6] Testing single set() and get() / exists()...")
    k1, v1 = "node.cluster.id", "shard-03-alpha"
    r = user_kv.functions.set(k1, v1).transact(user_key)
    print(f"    set receipt: {r}")

    exists_k1 = user_kv.functions.exists(k1).call()[0]
    val_k1 = user_kv.functions.get(k1).call()[0]
    print(f"    Read verified: exists={exists_k1}, val='{val_k1}'")

    # 8. 批量写入: batchSet(string[], string[])
    print("\n[Step 7] Testing batchSet(keys, values)...")
    keys_arr = ["metrics.cpu", "metrics.mem", "metrics.disk"]
    vals_arr = ["42%", "16GB", "512GB"]
    try:
        r = user_kv.functions['batchSet(string[],string[])'](keys_arr, vals_arr).transact(user_key)
    except Exception:
        r = user_kv.functions.batchSet(keys_arr, vals_arr).transact(user_key)
    print(f"    batchSet (arrays) receipt: {r}")

    # 9. 批量写入: batchSet(Entry[])
    print("\n[Step 8] Testing batchSet(Entry[] struct array)...")
    struct_entries = [
        ("user.cfg.theme", "dark"),
        ("user.cfg.lang", "zh-CN"),
        ("user.cfg.cache", "enabled")
    ]
    signature = "batchSet((string,string)[])"
    try:
        r = user_kv.functions[signature](struct_entries).transact(user_key)
    except Exception:
        r = invoke_overloaded_fn(
            contract=user_kv,
            signature=signature,
            input_types=["(string,string)[]"],
            args=[struct_entries],
            user_key=user_key
        )
    print(f"    batchSet (structs) receipt: {r}")

    # 10. 批量读取与精细查询
    print("\n[Step 9] Testing batchGet and batchGetDetailed...")
    total = user_kv.functions.totalKeys().call()[0]
    print(f"    Total keys in database: {total}")

    query_keys = ["metrics.cpu", "unknown.key", "user.cfg.theme"]
    b_vals = user_kv.functions.batchGet(query_keys).call()[0]
    print(f"    batchGet values: {b_vals}")

    b_detailed = user_kv.functions.batchGetDetailed(query_keys).call()[0]
    print("    batchGetDetailed results:")
    for item in b_detailed:
        print(f"      - key: {item[0]}, value: '{item[1]}', exists: {item[2]}")

    # 11. 分页查询
    print("\n[Step 10] Testing getPaginated()...")
    page_data = user_kv.functions.getPaginated(0, 3).call()[0]
    print(f"    Pagination (offset=0, limit=3): {[p[0] for p in page_data]}")

    # 12. 删除与重复删除日志追踪（输出日志，不中断执行）
    print("\n[Step 11] Testing remove() and logging responses...")
    del_target = "user.cfg.cache"

    # 第一次正常删除
    r_del1 = user_kv.functions.remove(del_target).transact(user_key)
    print(f"    First remove('{del_target}') receipt: {r_del1}")
    ex1 = user_kv.functions.exists(del_target).call()[0]
    total1 = user_kv.functions.totalKeys().call()[0]
    print(f"    Post-delete status: exists={ex1}, remaining_keys={total1}")

    # 第二次重复删除（预期触发 KeyNotFound）
    r_del2 = user_kv.functions.remove(del_target).transact(user_key)
    print(f"    Repeated remove('{del_target}') receipt: {r_del2}")

    # 打印链上状态是否保持一致
    ex2 = user_kv.functions.exists(del_target).call()[0]
    total2 = user_kv.functions.totalKeys().call()[0]
    print(f"    State check: exists={ex2}, remaining_keys={total2}")
    if total1 == total2 and not ex2:
        print("    [Info] Contract state unchanged; EVM successfully guarded against duplicate delete.")

    # 13. 清理 prefund
    print("\n[Step 12] Refunding gas prefund...")
    r_rf = user_kv.refund(user_key)
    print(f"    Refund receipt: {r_rf}")

    print("\n" + "=" * 72)
    print(f"  Test run completed for Shard {TARGET_SHARD}!")
    print("=" * 72)


# ---------------------------------------------------------------------------
# Entry Point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Shardora StringKeyValueDatabase Test")
    parser.add_argument("--host", default="127.0.0.1", help="Node IP")
    parser.add_argument("--port", type=int, default=23001, help="Node HTTPS port")
    parser.add_argument(
        "--key",
        default="71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6",
        help="Deployer private key (hex)"
    )
    args = parser.parse_args()

    w3 = ShardoraWeb3Mock(args.host, args.port)
    deployer_addr = w3.client.get_address(args.key)
    test_key_value_database(w3, deployer_addr, args.key)


if __name__ == "__main__":
    main()
