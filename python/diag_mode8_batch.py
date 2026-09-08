#!/usr/bin/env python3
"""
Mode 8 Phase 5 批量诊断
从 txcli 输出文件或 stdin 读取合约/用户信息，批量检查每个失败的跨分片传输。

用法：
  ./txcli 8 3 127.0.0.1 13001 2>&1 | tee txcli_out.txt
  python3 diag_mode8_batch.py txcli_out.txt

或者交互输入：
  python3 diag_mode8_batch.py
  (然后把 txcli 输出粘贴进来，Ctrl-D 结束)

若在公网访问，改 HOST：
  HOST=139.159.119.119 python3 diag_mode8_batch.py txcli_out.txt
"""
import sys, os, re, json, struct
import requests
from Crypto.Hash import keccak as _keccak
import eth_abi

HOST       = os.environ.get("HOST", "127.0.0.1")
SHARD_PORT = {2: 22001, 3: 23001, 4: 24001, 5: 25001, 6: 26001}
SSL        = False

# ── crypto ────────────────────────────────────────────────────────────────────

def keccak256(b):
    k = _keccak.new(digest_bits=256); k.update(b); return k.digest()

def sel4(sig): return keccak256(sig.encode())[:4].hex()

def abi_enc(sig, types, args):
    return sel4(sig) + eth_abi.encode(types, args).hex()

def abi_dec(raw, types):
    try:
        b = bytes.fromhex(raw.strip().lstrip("0x"))
        return eth_abi.decode(types, b)
    except: return None

# ── Feistel ───────────────────────────────────────────────────────────────────

_TAG = b"AKAVERSE_FEISTEL_V1"[:18]

def _rk(shard, pool, r):
    return keccak256(_TAG + shard.to_bytes(4,"big") + pool.to_bytes(4,"big") + r.to_bytes(4,"big"))[22:32]

def _F(R, rk):
    return keccak256(bytes(a^b for a,b in zip(R,rk)))[22:32]

def feistel_derive(base_hex, shard, pool):
    b = bytes.fromhex(base_hex.lower().zfill(40))
    L,R = bytearray(b[:10]), bytearray(b[10:])
    for i in range(4):
        rk = _rk(shard, pool, i)
        nR = bytearray(a^b for a,b in zip(L, _F(bytes(R), rk)))
        L,R = bytearray(R), nR
    return (bytes(L)+bytes(R)).hex()

def xxh32(data, seed=0):
    P1,P2,P3,P4,P5,M = 0x9E3779B1,0x85EBCA77,0xC2B2AE3D,0x27D4EB2F,0x165667B1,0xFFFFFFFF
    u=lambda v:v&M; r=lambda v,n:u((v<<n)|(v>>(32-n)))
    n,p=len(data),0
    if n>=16:
        v1,v2,v3,v4=u(seed+P1+P2),u(seed+P2),u(seed),u(seed-P1)
        while p<=n-16:
            for vi in range(4):
                l=struct.unpack_from("<I",data,p)[0];p+=4
                if vi==0:v1=u(r(u(v1+u(l*P2)),13)*P1)
                elif vi==1:v2=u(r(u(v2+u(l*P2)),13)*P1)
                elif vi==2:v3=u(r(u(v3+u(l*P2)),13)*P1)
                else:v4=u(r(u(v4+u(l*P2)),13)*P1)
        h=u(r(v1,1)+r(v2,7)+r(v3,12)+r(v4,18))
    else: h=u(seed+P5)
    h=u(h+n)
    while p<=n-4:h=u(r(u(h+u(struct.unpack_from("<I",data,p)[0]*P3)),17)*P4);p+=4
    while p<n:h=u(r(u(h+u(data[p]*P5)),11)*P1);p+=1
    h=u(u(h^(h>>15))*P2);h=u(u(h^(h>>13))*P3);h=u(h^(h>>16))
    return h

def pool_of(addr): return xxh32(bytes.fromhex(addr.lower()), 623453345) % 32

# ── HTTP ──────────────────────────────────────────────────────────────────────

def abi_query(shard, frm, contract, inp):
    port = SHARD_PORT.get(shard)
    if not port: return f"UNKNOWN_SHARD:{shard}"
    try:
        r = requests.post(f"https://{HOST}:{port}/abi_query_contract",
                          data={"from":frm,"address":contract,"input":inp},
                          verify=SSL, timeout=8)
        return r.text.strip()
    except Exception as e: return f"ERR:{e}"

def qaccount(shard, addr):
    port = SHARD_PORT.get(shard)
    if not port: return {}
    try:
        r = requests.get(f"https://{HOST}:{port}/query_account",
                         params={"addr":addr}, verify=SSL, timeout=8)
        return r.json()
    except Exception as e: return {"error":str(e)}

def bal_of(shard, frm, contract, who):
    inp = abi_enc("balanceOf(address)",["address"],[bytes.fromhex(who)])
    raw = abi_query(shard, frm, contract, inp)
    d   = abi_dec(raw, ["uint256"])
    return (d[0] if d else None), raw

def r_bool(shard, frm, contract, sig):
    raw = abi_query(shard, frm, contract, sel4(sig))
    d   = abi_dec(raw, ["bool"])
    return (d[0] if d else None), raw

def r_uint(shard, frm, contract, sig):
    raw = abi_query(shard, frm, contract, sel4(sig))
    d   = abi_dec(raw, ["uint256"])
    return (d[0] if d else None), raw

# ── Parse txcli output ────────────────────────────────────────────────────────
# Expected lines (example):
#   [Phase 3] token0 deployed: 5b344b8ac8742e6092ea692b49043cc84fb9f9dc  shard=3
#   [Phase 4] user0 addr: d31be15a431be08d2cc37ff3190924f0e7934169  shard=3  token=token0
#   [Phase 5] FAIL token0->user0  user_shard=3  expected=1000  got=0
#   deployer: 12b37a6e48066e4377a1d59ad1e20d7d473a4a1c

# Also handles compact "Phase5 FAIL" lines
TOKEN_RE   = re.compile(r'\[Phase\s*3\].*?token(\d+).*?deployed.*?:\s*([0-9a-fA-F]{40}).*?shard\s*[=:]\s*(\d+)', re.I)
USER_RE    = re.compile(r'\[Phase\s*[45]\].*?user(\d+).*?addr.*?:\s*([0-9a-fA-F]{40}).*?shard\s*[=:]\s*(\d+).*?token\s*[=:]\s*token(\d+)', re.I)
FAIL_RE    = re.compile(r'\[Phase\s*5\].*?FAIL.*?token(\d+).*?user(\d+).*?user_shard\s*[=:]\s*(\d+)', re.I)
DEPLOYER_RE= re.compile(r'deployer\s*[=:]\s*([0-9a-fA-F]{40})', re.I)
# Alternate compact pattern
COMPACT_RE = re.compile(r'FAIL\s+token(\d+)->user(\d+)\s+.*?shard=(\d+)', re.I)

def parse_txcli(text):
    tokens  = {}   # tid -> {addr, shard}
    users   = {}   # (tid,uid) -> {addr, shard}
    fails   = []   # [(tid,uid,shard)]
    deployer= None

    for line in text.splitlines():
        m = TOKEN_RE.search(line)
        if m: tokens[int(m.group(1))] = {"addr": m.group(2).lower(), "shard": int(m.group(3))}

        m = USER_RE.search(line)
        if m: users[(int(m.group(4)), int(m.group(1)))] = {"addr": m.group(2).lower(), "shard": int(m.group(3))}

        m = FAIL_RE.search(line) or COMPACT_RE.search(line)
        if m: fails.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))

        m = DEPLOYER_RE.search(line)
        if m: deployer = m.group(1).lower()

    return tokens, users, fails, deployer

# ── Diag one failure ──────────────────────────────────────────────────────────

def diag_one(token_addr, token_shard, deployer, user_addr, user_shard, label):
    print(f"\n{'─'*70}")
    print(f"  {label}")
    print(f"  token={token_addr} (shard {token_shard})")
    print(f"  user ={user_addr}  (shard {user_shard})")
    print(f"{'─'*70}")

    # deployer balance on source
    dep_bal, raw = bal_of(token_shard, deployer, token_addr, deployer)
    total, _     = r_uint(token_shard, deployer, token_addr, "totalSupply()")
    is_root, _   = r_bool(token_shard, deployer, token_addr, "IS_ROOT()")
    print(f"  [source shard {token_shard}] IS_ROOT={is_root}  totalSupply={total}  deployer_bal={dep_bal}  raw={raw[:40]}")

    if dep_bal == 0 or dep_bal is None:
        print("  *** DEPLOYER BALANCE = 0 — _baseInit() failed (constructor still broken?) ***")
    else:
        print(f"  OK deployer has {dep_bal/1e18:.2f} ether")

    # Feistel shadow
    up = pool_of(user_addr)
    shadow = feistel_derive(token_addr, user_shard, up)
    print(f"  [Feistel] user pool={up}  shadow={shadow}")

    # shadow account
    acc = qaccount(user_shard, shadow)
    print(f"  [target shard {user_shard}] account={json.dumps(acc)}")

    shadow_root, _ = r_bool(user_shard, deployer, shadow, "IS_ROOT()")
    user_bal, raw2 = bal_of(user_shard, deployer, shadow, user_addr)
    print(f"  [target shard {user_shard}] shadow IS_ROOT={shadow_root}  user_bal={user_bal}  raw={raw2[:40]}")

    if shadow_root is None:
        print("  *** SHADOW CONTRACT NOT DEPLOYED — consensus clone not created ***")
    elif user_bal == 0:
        print("  *** SHADOW EXISTS but user_bal=0 — systemExecuteCrossTransfer not called ***")
    else:
        print(f"  OK user has {user_bal/1e18:.2f} ether ← FIXED!")

# ── Entry ─────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f: text = f.read()
    else:
        print("Paste txcli output, then Ctrl-D:")
        text = sys.stdin.read()

    tokens, users, fails, deployer = parse_txcli(text)

    if not fails:
        print("No FAIL lines found. Try manual mode:")
        print("  python3 diag_mode8.py <contract> <deployer> <user> <target_shard> [source_shard]")
        return

    if not deployer:
        deployer = input("deployer address not found, enter manually: ").strip().lower()

    print(f"\nFound {len(fails)} failures. HOST={HOST}")
    print(f"tokens  : {tokens}")
    print(f"deployer: {deployer}")

    seen_tokens = set()
    for (tid, uid, user_shard) in fails:
        tok = tokens.get(tid)
        usr = users.get((tid, uid))
        if not tok:
            print(f"  [SKIP] token{tid} address unknown")
            continue
        if not usr:
            # try generic user address from uid
            print(f"  [SKIP] user{uid} address unknown for token{tid}")
            continue

        label = f"token{tid} -> user{uid}"
        diag_one(tok["addr"], tok["shard"], deployer,
                 usr["addr"], user_shard, label)

        # Only need to check each token once for deployer/shadow
        # (all users in same shard share same shadow contract)
        key = (tid, user_shard)
        if key in seen_tokens:
            print("  (same shadow as above — skip repeat)")
            continue
        seen_tokens.add(key)

if __name__ == "__main__":
    main()
