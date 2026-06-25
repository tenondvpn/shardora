import time
import secrets
import sys
import os

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'clipy')))
from shardora_sdk import ShardoraWeb3Mock, StepType, compile_and_link, to_checksum_address

PROXY_RE_SOL = """
// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract ProxyReencryption {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;
    struct ProxyInfo {
        bytes32 id;
        bytes32 res_info;
        bool exists;
    }

    struct TxGidInfo {
        bytes32 gid;
        bytes content;
        bytes32 id;
        bool exists;
    }

    event DebugEvent(
       uint256 value
    );

    event DebugEventBytes(
       bytes value
    );

    mapping(bytes32 => ProxyInfo) public proxy_map;
    mapping(bytes32 => TxGidInfo) public tx_gid_map;
    bytes32[] all_ids;
    bytes32[] all_gids;

    // SetUp：初始化算法，需要用到pbc库
    constructor(bytes memory enc_init_param) {
        enc_init_param_ = ripemd160(enc_init_param);
    }

    function CreatePrivateAndPublicKeys(bytes32 id, bytes32 gid, bytes memory content, bytes memory params) public {
        emit DebugEvent(0);
        require(!proxy_map[id].exists);
        require(!tx_gid_map[gid].exists);
        emit DebugEvent(1);
        bytes32 res = ripemd160(params);
        proxy_map[id] = ProxyInfo({
            id: id,
            res_info: res,
            exists: true
        });

        tx_gid_map[gid] = TxGidInfo({
            gid: gid,
            id: id,
            content: content,
            exists: true
        });

        all_ids.push(id);
        all_gids.push(gid);
        emit DebugEvent(2);
        emit DebugEvent(all_ids.length);
    }

    function CreateReEncryptionKeys(bytes32 id, bytes32 gid, bytes memory content, bytes memory params) public {
        emit DebugEvent(3);
        require(proxy_map[id].exists);
        emit DebugEvent(4);
        bytes32 res = ripemd160(params);
        require(!tx_gid_map[gid].exists);
        tx_gid_map[gid] = TxGidInfo({
            gid: gid,
            id: id,
            content: content,
            exists: true
        });

        all_gids.push(gid);
        emit DebugEvent(5);
    }

    function EncryptUserMessage(bytes32 id, bytes32 gid, bytes memory content, bytes memory params) public {
        emit DebugEvent(6);
        require(proxy_map[id].exists);
        emit DebugEvent(7);
        bytes32 res = ripemd160(params);
        require(!tx_gid_map[gid].exists);
        tx_gid_map[gid] = TxGidInfo({
            gid: gid,
            id: id,
            content: content,
            exists: true
        });

        all_gids.push(gid);
        emit DebugEvent(8);
    }

    function ReEncryptUserMessage(bytes32 id, bytes32 gid, bytes memory content, bytes memory params) public {
        emit DebugEvent(9);
        require(proxy_map[id].exists);
        emit DebugEvent(10);
        bytes32 res = ripemd160(params);
        require(!tx_gid_map[gid].exists);
        tx_gid_map[gid] = TxGidInfo({
            gid: gid,
            id: id,
            content: content,
            exists: true
        });

        all_gids.push(gid);
        emit DebugEvent(11);
    }

    function ReEncryptUserMessageWithMember(bytes32 id, bytes32 gid, bytes memory content, bytes memory params) public {
        emit DebugEvent(12);
        require(proxy_map[id].exists);
        emit DebugEvent(13);
        bytes32 res = ripemd160(params);
        require(!tx_gid_map[gid].exists);
        tx_gid_map[gid] = TxGidInfo({
            gid: gid,
            id: id,
            content: content,
            exists: true
        });

        all_gids.push(gid);
        emit DebugEvent(14);
    }

    function Decryption(bytes32 id, bytes32 gid, bytes memory content, bytes memory params) public {
        emit DebugEvent(15);
        require(proxy_map[id].exists);
        emit DebugEvent(16);
        bytes32 res = ripemd160(params);
        require(!tx_gid_map[gid].exists);
        tx_gid_map[gid] = TxGidInfo({
            gid: gid,
            id: id,
            content: content,
            exists: true
        });

        all_gids.push(gid);
        emit DebugEvent(17);
    }

    function Bytes32toBytes(bytes32 _data) public pure returns (bytes memory) {
        return abi.encodePacked(_data);
    }

    function bytesConcat(bytes[] memory arr, uint count) public pure returns (bytes memory){
        uint len = 0;
        for (uint i = 0; i < count; i++) {
            len += arr[i].length;
        }

        bytes memory bret = new bytes(len);
        uint k = 0;
        for (uint i = 0; i < count; i++) {
            for (uint j = 0; j < arr[i].length; j++) {
                bret[k++] = arr[i][j];
            }
        }

        return bret;
    }

    function ToHex(bytes memory buffer) public pure returns (bytes memory) {
        bytes memory converted = new bytes(buffer.length * 2);
        bytes memory _base = "0123456789abcdef";
        for (uint256 i = 0; i < buffer.length; i++) {
            converted[i * 2] = _base[uint8(buffer[i]) / _base.length];
            converted[i * 2 + 1] = _base[uint8(buffer[i]) % _base.length];
        }

        return converted;
    }

    function toBytes(address a) public pure returns (bytes memory) {
        return abi.encodePacked(a);
    }

    function u256ToBytes(uint256 x) public pure returns (bytes memory b) {
        b = new bytes(32);
        assembly { mstore(add(b, 32), x) }
    }

    function GetProxyJson(ProxyInfo memory ars, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"id":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(ars.id));
        all_bytes[filedCount++] = '","res":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(ars.res_info));
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetGidProxyJson(TxGidInfo memory ars, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"id":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(ars.id));
        all_bytes[filedCount++] = '","gid":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(ars.gid));
        all_bytes[filedCount++] = '","content":"';
        all_bytes[filedCount++] = ars.content;
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllProxyJson() public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_ids.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_ids.length;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetProxyJson(proxy_map[all_ids[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }

    function GetAllGidJson(bytes32 id) public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_gids.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_gids.length;
        for (uint i=0; i<arrayLength; i++) {
            if (tx_gid_map[all_gids[i]].id != id) {
                continue;
            }

            all_bytes[i + 1] = GetGidProxyJson(tx_gid_map[all_gids[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }

    function JustCallRipemd160(bytes memory params) public view returns(bytes memory) {
        bytes32 res = ripemd160(params);
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"res":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(res));
        all_bytes[filedCount++] = '"}';
        return bytesConcat(all_bytes, filedCount);
    }
}

"""
def run_comprehensive_proxy_demo():
    # --- 基础配置 ---
    IP, PORT = "127.0.0.1", 23001
    PRIV_KEY = "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6"
    
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY_ADDR = w3.client.get_address(PRIV_KEY)
    print(f"[*] Operator Address: {MY_ADDR}")

    # --- 1. 编译与部署 ---
    print("\n[1] Compiling and Deploying...")
    bytecode, abi = compile_and_link(PROXY_RE_SOL, "ProxyReencryption")
    
    # 构造函数参数
    init_data = b"system_init_pbc_params"
    contract = w3.shardora.contract(abi=abi, bytecode=bytecode, sender_address=MY_ADDR).deploy({
        'from': MY_ADDR,
        'salt': secrets.token_hex(32),
        'args': [init_data]
    }, PRIV_KEY)
    print(f"[+] Contract deployed at: {contract.address}")

    # --- 准备通用测试数据 ---
    # 我们需要一个基础 ID 来让后面的逻辑（require(proxy_map[id].exists)）通过
    base_id = secrets.token_bytes(32)
    
    # --- 2. 测试写入函数 (Transactions) ---
    
    # A. CreatePrivateAndPublicKeys (基础入口)
    print("\n[2-A] Executing: CreatePrivateAndPublicKeys")
    gid_1 = secrets.token_bytes(32)
    receipt = contract.functions.CreatePrivateAndPublicKeys(
        base_id, gid_1, b"content_v1", b"params_v1"
    ).transact(PRIV_KEY, prefund=10**8)
    print(f"    Status: {receipt['status']} | Events: {len(receipt.get('decoded_events', []))}")

    # B. CreateReEncryptionKeys
    print("[2-B] Executing: CreateReEncryptionKeys")
    gid_2 = secrets.token_bytes(32)
    contract.functions.CreateReEncryptionKeys(base_id, gid_2, b"re_enc_content", b"re_enc_params").transact(PRIV_KEY)

    # C. EncryptUserMessage
    print("[2-C] Executing: EncryptUserMessage")
    gid_3 = secrets.token_bytes(32)
    contract.functions.EncryptUserMessage(base_id, gid_3, b"user_msg", b"enc_params").transact(PRIV_KEY)

    # D. ReEncryptUserMessage
    print("[2-D] Executing: ReEncryptUserMessage")
    gid_4 = secrets.token_bytes(32)
    contract.functions.ReEncryptUserMessage(base_id, gid_4, b"re_msg", b"re_params").transact(PRIV_KEY)

    # E. Decryption
    print("[2-E] Executing: Decryption")
    gid_5 = secrets.token_bytes(32)
    contract.functions.Decryption(base_id, gid_5, b"dec_msg", b"dec_params").transact(PRIV_KEY)

    # --- 3. 测试查询函数 (Calls) ---
    print("\n[3] Testing Call Functions...")
    time.sleep(2) # 等待状态落库

    # A. GetAllProxyJson
    print("[3-A] GetAllProxyJson:")
    raw_proxies = contract.functions.GetAllProxyJson().call()
    print(f"    Proxies JSON: {raw_proxies}")

    # B. GetAllGidJson (根据指定的 base_id 查询)
    print("[3-B] GetAllGidJson (Filtered by ID):")
    raw_gids = contract.functions.GetAllGidJson(base_id).call()
    print(f"    GIDs JSON: {raw_gids}")

    # C. JustCallRipemd160 (工具类函数测试)
    print("[3-C] JustCallRipemd160 (Pure Tooling Test):")
    ripemd_res = contract.functions.JustCallRipemd160(b"hello_shardora").call()
    print(f"    Tooling Result: {ripemd_res}")

    # D. 直接访问 Public Mapping (proxy_map)
    print("[3-D] Accessing Public Mapping (proxy_map):")
    # 注意：mapping 的自动 getter 需要传入 Key，返回的是 struct 的字段元组
    proxy_info = contract.functions.proxy_map(base_id).call()
    print(f"    Manual Mapping Query: {proxy_info}")

    print("\n[!] All functions called successfully.")

if __name__ == "__main__":
    run_comprehensive_proxy_demo()