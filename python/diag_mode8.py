#!/usr/bin/env python3
"""
Mode 8 Phase 5 诊断脚本
用法：
  python3 diag_mode8.py <contract_addr> <deployer_addr> <target_user_addr> <target_shard>

示例（从 txcli 输出里取合约地址和 deployer 地址）：
  python3 diag_mode8.py 5b344b8ac8742e6092ea692b49043cc84fb9f9dc \
                        12b37a6e48066e4377a1d59ad1e20d7d473a4a1c \
                        d31be15a431be08d2cc37ff3190924f0e7934169 3

脚本会：
1. 查 deployer 在源分片合约里的余额（_balances[deployer]）→ 确认构造函数是否正常
2. 查 Feistel 派生的 shadow 合约地址
3. 查 shadow 合约上 target_user 的余额
4. 查 shadow 合约是否存在（account query）
"""
import sys, json, struct, time
import requests
import eth_abi
from Crypto.Hash import keccak as _keccak

HOST = "127.0.0.1"
SHARD_PORT = {2: 22001, 3: 23001, 4: 24001, 5: 25001, 6: 26001}
VERIFY_SSL = False

# ── Crypto ────────────────────────────────────────────────────────────────────

def keccak256(data: bytes) -> bytes:
    k = _keccak.new(digest_bits=256)
    k.update(data)
    return k.digest()

def sel(sig: str) -> str:
    return keccak256(sig.encode())[:4].hex()

def encode_call(sig: str, types, args) -> str:
    return sel(sig) + eth_abi.encode(types, args).hex()

def decode_result(raw: str, types):
    try:
        b = bytes.fromhex(raw.strip().lstrip("0x").lstrip("0X"))
        return eth_abi.decode(types, b)
    except Exception as e:
        return None

# ── Feistel (matches C++ reversible_feistel_address.h) ────────────────────────

_TAG = b"AKAVERSE_FEISTEL_V1"[:18]

def _rk(shard, pool, r):
    pre = _TAG + shard.to_bytes(4,"big") + pool.to_bytes(4,"big") + r.to_bytes(4,"big")
    return keccak256(pre)[22:32]

def _F(R, rk):
    return keccak256(bytes(a^b for a,b in zip(R, rk)))[22:32]

def feistel_derive(base_hex: str, shard: int, pool: int) -> str:
    b = bytes.fromhex(base_hex.lower().zfill(40))
    L, R = bytearray(b[:10]), bytearray(b[10:])
    for i in range(4):
        rk = _rk(shard, pool, i)
        nR = bytearray(a^b for a,b in zip(L, _F(bytes(R), rk)))
        L, R = bytearray(R), nR
    return (bytes(L)+bytes(R)).hex()

def xxh32(data: bytes, seed=0) -> int:
    P1,P2,P3,P4,P5,M = 0x9E3779B1,0x85EBCA77,0xC2B2AE3D,0x27D4EB2F,0x165667B1,0xFFFFFFFF
    u = lambda v: v & M
    r = lambda v,n: u((v<<n)|(v>>(32-n)))
    n,p = len(data),0
    if n >= 16:
        v1,v2,v3,v4 = u(seed+P1+P2),u(seed+P2),u(seed),u(seed-P1)
        while p <= n-16:
            for vi in range(4):
                l = struct.unpack_from("<I",data,p)[0]; p+=4
                if vi==0: v1=u(r(u(v1+u(l*P2)),13)*P1)
                elif vi==1: v2=u(r(u(v2+u(l*P2)),13)*P1)
                elif vi==2: v3=u(r(u(v3+u(l*P2)),13)*P1)
                else: v4=u(r(u(v4+u(l*P2)),13)*P1)
        h = u(r(v1,1)+r(v2,7)+r(v3,12)+r(v4,18))
    else:
        h = u(seed+P5)
    h = u(h+n)
    while p<=n-4: h=u(r(u(h+u(struct.unpack_from("<I",data,p)[0]*P3)),17)*P4); p+=4
    while p<n: h=u(r(u(h+u(data[p]*P5)),11)*P1); p+=1
    h=u(u(h^(h>>15))*P2); h=u(u(h^(h>>13))*P3); h=u(h^(h>>16))
    return h

def pool_of(addr: str) -> int:
    return xxh32(bytes.fromhex(addr.lower()), 623453345) % 32

# ── HTTP ──────────────────────────────────────────────────────────────────────

def abi_query(shard: int, from_addr: str, contract: str, inp: str) -> str:
    port = SHARD_PORT[shard]
    url  = f"https://{HOST}:{port}/abi_query_contract"
    try:
        r = requests.post(url, data={"from": from_addr, "address": contract, "input": inp},
                          verify=VERIFY_SSL, timeout=8)
        return r.text.strip()
    except Exception as e:
        return f"ERROR:{e}"

def query_account(shard: int, addr: str) -> dict:
    port = SHARD_PORT[shard]
    url  = f"https://{HOST}:{port}/query_account"
    try:
        r = requests.get(url, params={"addr": addr}, verify=VERIFY_SSL, timeout=8)
        return r.json()
    except Exception as e:
        return {"error": str(e)}

# ── Selectors ─────────────────────────────────────────────────────────────────

SEL_BALANCE_OF = sel("balanceOf(address)")
SEL_IS_ROOT    = sel("IS_ROOT()")
SEL_BASE_ROOT  = sel("BASE_ROOT_ADDRESS()")
SEL_SYS_EXEC   = sel("SYSTEM_EXECUTOR()")
SEL_TOTAL      = sel("totalSupply()")

def balance_of(shard, from_a, contract, target):
    inp = encode_call("balanceOf(address)", ["address"], [bytes.fromhex(target)])
    raw = abi_query(shard, from_a, contract, inp)
    d = decode_result(raw, ["uint256"])
    return d[0] if d else None, raw

def read_bool(shard, from_a, contract, sig):
    raw = abi_query(shard, from_a, contract, sel(sig))
    d = decode_result(raw, ["bool"])
    return d[0] if d else None, raw

def read_addr(shard, from_a, contract, sig):
    raw = abi_query(shard, from_a, contract, sel(sig))
    d = decode_result(raw, ["address"])
    return d[0].lower() if d else None, raw

def read_uint(shard, from_a, contract, sig):
    raw = abi_query(shard, from_a, contract, sel(sig))
    d = decode_result(raw, ["uint256"])
    return d[0] if d else None, raw

# ── Main ──────────────────────────────────────────────────────────────────────

def diag_token(base_contract: str, deployer: str, target_user: str, target_shard: int,
               source_shard: int = None):
    """
    base_contract  : hex40, token 合约地址（部署在 source_shard）
    deployer       : hex40, 部署者地址（_baseInit 时 msg.sender）
    target_user    : hex40, 收款用户地址（在 target_shard）
    target_shard   : int,   收款用户所在分片
    source_shard   : int,   合约所在分片（None 则自动检测）
    """
    # 1. 自动找 source shard
    if source_shard is None:
        for s, p in SHARD_PORT.items():
            acc = query_account(s, base_contract)
            if acc.get("balance") is not None or acc.get("nonce") is not None:
                source_shard = s
                print(f"  [auto] base contract found on shard {s}")
                break
        if source_shard is None:
            print("  [ERROR] cannot find base contract on any shard")
            return

    print(f"\n{'='*60}")
    print(f"  base_contract : {base_contract}  (shard {source_shard})")
    print(f"  deployer      : {deployer}")
    print(f"  target_user   : {target_user}  (shard {target_shard})")
    print(f"{'='*60}")

    # 2. 查 source shard 合约状态
    print(f"\n[1] source shard {source_shard} — base contract")
    is_root, _ = read_bool(source_shard, deployer, base_contract, "IS_ROOT()")
    base_root,_= read_addr(source_shard, deployer, base_contract, "BASE_ROOT_ADDRESS()")
    sys_exec, _= read_addr(source_shard, deployer, base_contract, "SYSTEM_EXECUTOR()")
    total, _   = read_uint(source_shard, deployer, base_contract, "totalSupply()")
    dep_bal, _ = balance_of(source_shard, deployer, base_contract, deployer)

    print(f"  IS_ROOT           = {is_root}")
    print(f"  BASE_ROOT_ADDRESS = {base_root}")
    print(f"  SYSTEM_EXECUTOR   = {sys_exec}")
    print(f"  totalSupply       = {total} ({(total/1e18):.2f} ether)" if total else "  totalSupply = None")
    print(f"  balanceOf(deployer)= {dep_bal} ({(dep_bal/1e18):.2f} ether)" if dep_bal is not None else "  balanceOf(deployer) = None")

    if dep_bal == 0 or dep_bal is None:
        print("  *** DEPLOYER HAS NO BALANCE — constructor _baseInit() did not run properly ***")
    else:
        print("  OK: deployer has balance → crossTransfer should work")

    # 3. 计算 shadow 合约地址
    user_pool = pool_of(target_user)
    shadow = feistel_derive(base_contract, target_shard, user_pool)
    print(f"\n[2] Feistel shadow address")
    print(f"  target_user pool = {user_pool}")
    print(f"  shadow addr      = {shadow}  (on shard {target_shard})")

    # 4. 查 shadow 合约账户是否存在
    print(f"\n[3] target shard {target_shard} — shadow contract account")
    acc = query_account(target_shard, shadow)
    print(f"  account info: {json.dumps(acc)}")

    # 5. 查 shadow 合约状态
    print(f"\n[4] target shard {target_shard} — shadow contract state")
    shadow_is_root, _ = read_bool(target_shard, deployer, shadow, "IS_ROOT()")
    shadow_base, _    = read_addr(target_shard, deployer, shadow, "BASE_ROOT_ADDRESS()")
    shadow_sys, _     = read_addr(target_shard, deployer, shadow, "SYSTEM_EXECUTOR()")
    shadow_total, _   = read_uint(target_shard, deployer, shadow, "totalSupply()")
    user_bal, _       = balance_of(target_shard, deployer, shadow, target_user)

    print(f"  IS_ROOT           = {shadow_is_root}")
    print(f"  BASE_ROOT_ADDRESS = {shadow_base}")
    print(f"  SYSTEM_EXECUTOR   = {shadow_sys}")
    print(f"  totalSupply       = {shadow_total}")
    print(f"  balanceOf(user)   = {user_bal}")

    if shadow_is_root is None and shadow_base is None:
        print("  *** SHADOW CONTRACT NOT DEPLOYED on this shard ***")
        print("  → cross-shard clone deploy by consensus layer has not happened")
    elif user_bal == 0:
        print("  *** SHADOW EXISTS but user balance = 0 ***")
        print("  → systemExecuteCrossTransfer was not called (cross-shard msg not delivered)")
    else:
        print(f"  OK: user balance = {user_bal/1e18:.2f} ether")

    print()


if __name__ == "__main__":
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)

    base_contract = sys.argv[1].lower()
    deployer      = sys.argv[2].lower()
    target_user   = sys.argv[3].lower()
    target_shard  = int(sys.argv[4])
    source_shard  = int(sys.argv[5]) if len(sys.argv) > 5 else None

    diag_token(base_contract, deployer, target_user, target_shard, source_shard)
