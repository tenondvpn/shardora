#!/usr/bin/env python3
"""
异常交易 / 重放交易构造与验证脚本

复现 C++ 侧 http_handler.cc + tx_pool_manager.cc 的校验路径，构造下列非法
交易并观察节点返回的拒绝原因：

  用例 1  篡改签名任意字节        → 期望 kSignatureInvalid / kTxInvalidSignature
  用例 2  A 的地址配 B 的私钥签名  → 期望 kSignatureInvalid
  用例 3  截断签名 sign_r/sign_s  → 期望 kSignatureInvalid
  用例 4  畸形签名字段（非 hex）   → 期望 kSignatureInvalid / 参数解析失败
  用例 5  nonce 重放：合法交易上链后重复提交 3 次 → 期望 kTxUserNonceInvalid

用例 2 的语义：pubkey 取自 A（因此 from == A，账户存在性检查通过），但
sign 是用 B 的私钥生成的，节点 Verify(hash, A_pub, sign_B) == false。

Usage:
  python3 test_invalid_tx.py --host 10.61.1.50 --port 23001 --key <A私钥hex>
  python3 test_invalid_tx.py --host 10.61.1.50 --port 23001 --key <A> --to <目标地址> \
          --amount 1 --skip-replay
"""

from __future__ import annotations

import argparse
import hashlib
import os
import secrets
import struct
import sys
import time

import requests
import urllib3

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ecdsa import SECP256k1, SigningKey
from ecdsa.util import sigencode_string_canonize
from Crypto.Hash import keccak

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

GAS_LIMIT = 5000000
GAS_PRICE = 1

# 与 http_handler.cc 中 HttpStatusCode / transport_utils.h 的状态码保持一致
HTTP_STATUS_TEXT = {
    0: "kHttpSuccess",
    1: "kHttpError",
    2: "kAccountNotExists",
    3: "kBalanceInvalid",
    4: "kShardIdInvalid",
    5: "kSignatureInvalid",
    6: "kFromEqualToInvalid",
}


class SendRejected(Exception):
    """节点同步拒绝了本次提交（HTTP body 中带有失败状态）。"""

    def __init__(self, status, detail, body):
        super().__init__(detail)
        self.status = status
        self.detail = detail
        self.body = body


def keccak256(data: bytes) -> bytes:
    return keccak.new(digest_bits=256).update(data).digest()


def hexdecode(s: str) -> bytes:
    """Pod 侧 common::Encode::HexDecode 的近似：容忍 0x 前缀，奇数长度补 0。"""
    if s is None:
        return b""
    clean = str(s).strip().lower()
    if clean.startswith("0x"):
        clean = clean[2:]
    if len(clean) % 2:
        clean = "0" + clean
    try:
        return bytes.fromhex(clean)
    except ValueError:
        return b""


def derive(pk_hex: str):
    """返回 (私钥hex, 65字节公钥hex, 20字节地址hex)。

    公钥必须是 65 字节未压缩形式（含 0x04 前缀）：节点侧 ToAddressWithPublicKey
    对 65 字节走 keccak256(pub[1:65])，且待签原文里 pubkey 也是这 65 字节。
    """
    pk_hex = pk_hex.strip().lower().replace("0x", "")
    sk = SigningKey.from_string(bytes.fromhex(pk_hex), curve=SECP256k1)
    pub65 = sk.verifying_key.to_string("uncompressed")  # 65 字节，04||X||Y
    return pk_hex, pub65.hex(), keccak256(pub65[1:])[-20:].hex()


def derive_from_seed(seed: bytes) -> str:
    """确定性地从种子派生一个私钥 hex，便于复现。"""
    return hashlib.sha256(seed).hexdigest()


def build_msg(nonce, pub_hex, to_hex, amount, gas_limit, gas_price, step,
              contract_code="", input_hex="", prefund=0) -> bytes:
    """按 SDK 的字节序拼装待签名原文。"""
    msg = bytearray()
    msg.extend(struct.pack("<Q", nonce))
    msg.extend(bytes.fromhex(pub_hex))
    msg.extend(bytes.fromhex(to_hex))
    msg.extend(struct.pack("<Q", amount))
    msg.extend(struct.pack("<Q", gas_limit))
    msg.extend(struct.pack("<Q", gas_price))
    msg.extend(struct.pack("<Q", int(step)))
    if contract_code:
        msg.extend(bytes.fromhex(contract_code))
    if input_hex:
        msg.extend(bytes.fromhex(input_hex))
    if prefund > 0:
        msg.extend(struct.pack("<Q", prefund))
    return bytes(msg)


def sign_tx(pk_hex, msg: bytes) -> bytes:
    """返回 64 字节 r||s（可规范化）。"""
    sk = SigningKey.from_string(bytes.fromhex(pk_hex), curve=SECP256k1)
    txh = keccak256(msg)
    return sk.sign_digest_deterministic(
        txh, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize
    )


def build_payload(nonce, pub_hex, to_hex, amount, sig_r_hex, sig_s_hex, sign_v="0",
                  step=0, gas_limit=GAS_LIMIT, gas_price=GAS_PRICE,
                  contract_code="", input_hex="", prefund=0) -> dict:
    data = {
        "nonce": str(nonce),
        "pubkey": pub_hex,
        "to": to_hex,
        "amount": str(amount),
        "gas_limit": str(gas_limit),
        "gas_price": str(gas_price),
        "shard_id": "0",
        "type": str(int(step)),
        "sign_r": sig_r_hex,
        "sign_s": sig_s_hex,
        "sign_v": str(sign_v),
    }
    if contract_code:
        data["bytes_code"] = contract_code
    if input_hex:
        data["input"] = input_hex
    if prefund:
        data["prefund"] = str(prefund)
    return data


def parse_reject(body: str, http_code: int):
    """从 HTTP 响应体解析拒绝原因。同步拒绝 → (状态码, 描述)；接受 → (None, None)。"""
    b = (body or "").strip()
    if b in ("ok", "ok\n", ""):
        if http_code == 200:
            return None, None
        return -1, f"HTTP {http_code} empty body"

    if b.startswith("transaction invalid:"):
        name = b.split(":", 1)[1].strip()
        code = next((k for k, v in HTTP_STATUS_TEXT.items() if v == name), -1)
        return code, name

    if b.startswith("address invalid:"):
        return 2, "address invalid (账户未在链上注册)"

    # 参数解析失败（nonce/amount/gas/shard_id/sign_v 非整数等）
    for field in ("nonce", "amount", "gas_limit", "gas_price", "shard_id", "sign_v"):
        if b.startswith(f"{field} not integer"):
            return -1, b

    if "not ready" in b:
        return -1, b

    return -1, f"HTTP {http_code}: {b[:120]}"


def dump_tx(label, msg, pub_hex, to_hex, payload, sig=None):
    """打印待签原文、哈希、签名与 POST 字段，便于与 C++ GetTxMessageHash 逐字段对照。"""
    txh = keccak256(msg)
    print(f"    ── {label} 交易详情 ──")
    print(f"      待签原文(msg) len={len(msg)}: {msg.hex()}")
    print(f"      msg 字段序: nonce(8LE) | pubkey({len(pub_hex)//2}B) | "
          f"to(20B) | amount(8LE) | gas_limit(8LE) | gas_price(8LE) | step(8LE)")
    print(f"      tx_hash(keccak256(msg)): {txh.hex()}")
    print(f"      pubkey: {pub_hex}")
    print(f"      to:     {to_hex}")
    if sig is not None:
        print(f"      sign(r||s) len={len(sig)}: {bytes(sig).hex()}")
        print(f"      sign_r: {bytes(sig[:32]).hex()}")
        print(f"      sign_s: {bytes(sig[32:64]).hex()}")
    print(f"      POST 字段:")
    for k in ("nonce", "pubkey", "to", "amount", "gas_limit", "gas_price",
              "shard_id", "type", "sign_r", "sign_s", "sign_v",
              "bytes_code", "input", "prefund"):
        if k in payload:
            v = payload[k]
            shown = v if len(str(v)) <= 80 else f"{str(v)[:80]}...(len={len(str(v))})"
            print(f"        {k} = {shown}")


def post_tx(base_url, payload, timeout=10, show=False):
    """返回 (http_code, body, (status, detail))。"""
    if show:
        print(f"      POST {base_url}/transaction")
    try:
        r = requests.post(f"{base_url}/transaction", data=payload,
                          verify=False, timeout=timeout)
    except Exception as e:
        return 0, f"<连接异常: {e}>", (-1, str(e))
    status, detail = parse_reject(r.text, r.status_code)
    if show:
        print(f"      http={r.status_code} body={r.text.strip()!r}")
    return r.status_code, r.text.strip(), (status, detail)


def query_account(base_url, address):
    """返回账号 dict；不存在时返回 {}。"""
    try:
        r = requests.post(f"{base_url}/query_account", data={"address": address},
                          verify=False, timeout=5)
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return {}


def query_nonce(base_url, address) -> int:
    return int(query_account(base_url, address).get("nonce", 0))


def query_balance(base_url, address) -> int:
    return int(query_account(base_url, address).get("balance", 0))


def query_receipt(base_url, tx_hash):
    try:
        r = requests.post(f"{base_url}/transaction_receipt", data={"tx_hash": tx_hash},
                          verify=False, timeout=5)
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return {}


PENDING_STATUS = {10001, 10003}   # kMessageHandle / kTxAccept
NOT_EXISTS_STATUS = 100010        # kNotExists


def wait_final(base_url, tx_hash, timeout=120, not_exists_retries=10):
    """轮询回执直到得到终态；返回最终回执 dict（超时返回 None）。"""
    deadline = time.time() + timeout
    missing = 0
    while time.time() < deadline:
        resp = query_receipt(base_url, tx_hash)
        st = resp.get("status")
        if st is None:
            time.sleep(1)
            continue
        if st == NOT_EXISTS_STATUS:
            missing += 1
            if missing >= not_exists_retries:
                return resp
            time.sleep(1)
            continue
        if st in PENDING_STATUS:
            missing = 0
            time.sleep(1)
            continue
        return resp
    return None


# ── 用例实现 ─────────────────────────────────────────────────────────────────

def case1_tamper(base_url, pk_a, pub_a, to_hex, nonce, amount):
    """用例 1：篡改签名任意字节。"""
    msg = build_msg(nonce, pub_a, to_hex, amount, GAS_LIMIT, GAS_PRICE, 0)
    sig = bytearray(sign_tx(pk_a, msg))
    idx = secrets.randbelow(len(sig))
    old = sig[idx]
    sig[idx] ^= 0x01
    print(f"    篡改位置: sign 第 {idx} 字节  0x{old:02x} → 0x{sig[idx]:02x}")
    payload = build_payload(nonce, pub_a, to_hex, amount,
                            bytes(sig[:32]).hex(), bytes(sig[32:64]).hex())
    dump_tx("用例1 篡改签名", msg, pub_a, to_hex, payload, sig)
    return post_tx(base_url, payload, show=True)


def case2_wrong_key(base_url, pub_a, pk_b, to_hex, nonce, amount):
    """用例 2：用 A 的地址(pubkey)配 B 的私钥签名。"""
    msg = build_msg(nonce, pub_a, to_hex, amount, GAS_LIMIT, GAS_PRICE, 0)
    sig = sign_tx(pk_b, msg)
    _, pub_b, addr_b = derive(pk_b)
    print(f"    pubkey=A({pub_a[:16]}...)  sign=B(私钥对应地址 {addr_b})")
    payload = build_payload(nonce, pub_a, to_hex, amount,
                            sig[:32].hex(), sig[32:64].hex())
    dump_tx("用例2 A地址+B私钥", msg, pub_a, to_hex, payload, sig)
    return post_tx(base_url, payload, show=True)


def case3_truncate(base_url, pk_a, pub_a, to_hex, nonce, amount):
    """用例 3：截断 sign_r / sign_s。"""
    results = []
    for label, trim in (("sign_r 截断到 16 字节", 16), ("sign_s 截断到 8 字节", 8)):
        msg = build_msg(nonce, pub_a, to_hex, amount, GAS_LIMIT, GAS_PRICE, 0)
        sig = sign_tx(pk_a, msg)
        if label.startswith("sign_r"):
            r_hex, s_hex = sig[:trim].hex(), sig[32:64].hex()
        else:
            r_hex, s_hex = sig[:32].hex(), sig[32:32 + trim].hex()
        print(f"    {label}: sign_r(len={len(r_hex)}), sign_s(len={len(s_hex)})")
        payload = build_payload(nonce, pub_a, to_hex, amount, r_hex, s_hex)
        dump_tx(f"用例3 {label} (原始签名)", msg, pub_a, to_hex, payload, sig)
        results.append((label, post_tx(base_url, payload, show=True)))
    return results


def case4_malformed(base_url, pk_a, pub_a, to_hex, nonce, amount):
    """用例 4：畸形签名字段 —— 非 hex 字符 / 空字段。"""
    results = []
    msg = build_msg(nonce, pub_a, to_hex, amount, GAS_LIMIT, GAS_PRICE, 0)
    sig = sign_tx(pk_a, msg)

    variants = [
        ("sign_r 含非 hex 字符", "zz" * 16, sig[32:64].hex()),
        ("sign_s 含非 hex 字符", sig[:32].hex(), "QQ" * 16),
        ("sign_r 为空", "", sig[32:64].hex()),
        ("sign_s 为空", sig[:32].hex(), ""),
    ]
    for label, r_hex, s_hex in variants:
        print(f"    {label}: sign_r='{r_hex[:24]}'(len={len(r_hex)}), "
              f"sign_s len={len(s_hex)}")
        payload = build_payload(nonce, pub_a, to_hex, amount, r_hex, s_hex)
        dump_tx(f"用例4 {label} (原始签名)", msg, pub_a, to_hex, payload, sig)
        results.append((label, post_tx(base_url, payload, show=True)))
    return results


def case5_replay(base_url, pk_a, pub_a, to_hex, amount, wait_timeout=120):
    """用例 5：合法交易上链后，完全相同的交易原文重复提交 3 次。"""
    addr_a = pub_to_addr(pub_a)
    start_nonce = query_nonce(base_url, addr_a)
    nonce = start_nonce + 1
    print(f"    账号 {addr_a} 起始 nonce={start_nonce}，使用 nonce={nonce}")
    print(f"    余额: {query_balance(base_url, addr_a)}")

    msg = build_msg(nonce, pub_a, to_hex, amount, GAS_LIMIT, GAS_PRICE, 0)
    sig = sign_tx(pk_a, msg)
    txh = keccak256(msg).hex()
    payload = build_payload(nonce, pub_a, to_hex, amount,
                            sig[:32].hex(), sig[32:64].hex())

    dump_tx("用例5 合法交易", msg, pub_a, to_hex, payload, sig)
    http_code, body, (status, detail) = post_tx(base_url, payload, show=True)
    print(f"    [首次提交] http={http_code} body={body!r}")
    if status is not None:
        print("    [注意] 合法交易被同步拒绝，下面的重放结果不能作为 nonce 判重依据")
        rejected = []
        for i in range(3):
            code, body, (st, dt) = post_tx(base_url, payload, show=True)
            rejected.append((f"重放 {i + 1}", (code, body, st, dt)))
        expected = "kSignatureInvalid"
        print(f"    [诊断] 合法交易被判 {detail}，说明签名构造有误，"
              f"重放结果不代表 nonce 判重")
        return txh, None, rejected

    print(f"    等待上链 (最多 {wait_timeout}s) ...")
    receipt = wait_final(base_url, txh, timeout=wait_timeout)
    if receipt is None:
        print("    [警告] 等待回执超时，仍继续做重放测试")
        final_nonce = query_nonce(base_url, addr_a)
    else:
        print(f"    回执: {receipt}")
        final_nonce = query_nonce(base_url, addr_a)
    print(f"    上链后账号 nonce={final_nonce}")

    results = []
    for i in range(3):
        http_code, body, (status, detail) = post_tx(base_url, payload)
        print(f"    [重放 {i + 1}/3] http={http_code} body={body!r}")
        results.append((f"重放 {i + 1}", (http_code, body, status, detail)))
        time.sleep(0.3)
    return txh, receipt, results


def pub_to_addr(pub_hex: str) -> str:
    """由 65 字节（或 64 字节）公钥推地址，与 derive() 保持一致。"""
    b = bytes.fromhex(pub_hex)
    if len(b) == 65:
        b = b[1:]
    return keccak256(b)[-20:].hex()


# ── 结果判定 ─────────────────────────────────────────────────────────────────

def judge(label, result, expect_status, expect_text):
    """result 是 (http_code, body, (status, detail)) 或 (http_code, body, status, detail)。"""
    if len(result) == 4:
        http_code, body, status, detail = result
    else:
        http_code, body, (status, detail) = result
    text = body or ""
    hit_status = status in expect_status
    hit_text = any(t in text for t in expect_text)
    ok = hit_status or hit_text
    mark = "PASS" if ok else "FAIL"
    expect_desc = "/".join(expect_text)
    print(f"    [{mark}] {label}: 期望 {expect_desc} | 实际 status={status} "
          f"detail={detail!r}")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Shardora 异常交易 / nonce 重放测试",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host", default="10.61.1.50", help="节点 IP (默认 10.61.1.50)")
    parser.add_argument("--port", type=int, default=23001,
                        help="节点查询端口 (默认 23001 = shard 3)")
    parser.add_argument("--key", required=True, help="发送方 A 的私钥 hex")
    parser.add_argument("--to", default=None, help="接收地址 hex (默认随机生成)")
    parser.add_argument("--amount", type=int, default=1, help="转账金额 (默认 1)")
    parser.add_argument("--wait-timeout", type=int, default=120,
                        help="等待上链超时秒数 (默认 120)")
    parser.add_argument("--skip-replay", action="store_true",
                        help="跳过用例 5（nonce 重放，需要账号有余额）")
    parser.add_argument("--only", default=None,
                        help="只跑指定用例，如 --only 5")
    args = parser.parse_args()

    base_url = f"https://{args.host}:{args.port}"
    pk_a, pub_a, addr_a = derive(args.key)

    if args.to:
        to_hex = args.to.strip().lower().replace("0x", "")
        if len(to_hex) != 40:
            print(f"--to 必须是 40 位 hex 地址，实际 {len(to_hex)} 位")
            return 2
    else:
        to_hex = derive(derive_from_seed(b"invalid-tx-test-recipient"))[2]
        print(f"[info] 未指定 --to，使用随机接收地址 {to_hex}")

    pk_b = derive_from_seed(secrets.token_bytes(32))
    _, pub_b, addr_b = derive(pk_b)

    print("=" * 78)
    print("Shardora 异常交易 / nonce 重放测试")
    print("=" * 78)
    print(f"节点:        {base_url}")
    print(f"发送方 A:    地址 {addr_a}")
    print(f"             pubkey {pub_a}")
    print(f"接收地址:     {to_hex}")
    print(f"对端私钥 B:  地址 {addr_b}  (仅用例 2 使用)")

    account = query_account(base_url, addr_a)
    print(f"A 账户:      {account if account else '(链上不存在)'}")
    if account:
        print(f"A 余额/nonce: {account.get('balance')} / {account.get('nonce')}")
    else:
        print("[warn] A 账户在链上不存在 —— 合法交易会被 'address invalid' 拒绝，"
              "请先给 A 转币")

    # 每个用例使用不同 nonce，避免非签名字段差异影响判定
    base_nonce = query_nonce(base_url, addr_a)
    results = {}

    only = args.only
    want = lambda n: only is None or only == str(n)

    if want(1):
        print("\n[用例 1] 篡改签名任意字节")
        results[1] = [judge("篡改签名",
                            case1_tamper(base_url, args.key, pub_a, to_hex,
                                         base_nonce + 100, args.amount),
                            {5}, ("kSignatureInvalid", "kTxInvalidSignature"))]

    if want(2):
        print("\n[用例 2] A 的地址配 B 的私钥签名")
        results[2] = [judge("地址/私钥不匹配",
                            case2_wrong_key(base_url, pub_a, pk_b, to_hex,
                                            base_nonce + 101, args.amount),
                            {5}, ("kSignatureInvalid", "kTxInvalidSignature"))]

    if want(3):
        print("\n[用例 3] 截断 / 畸形签名：sign_r / sign_s 截断")
        results[3] = [judge(label, res, {5},
                            ("kSignatureInvalid", "kTxInvalidSignature"))
                      for label, res in case3_truncate(base_url, args.key, pub_a,
                                                       to_hex, base_nonce + 102,
                                                       args.amount)]

    if want(4):
        print("\n[用例 4] 畸形签名字段（非 hex / 空）")
        results[4] = [judge(label, res, {5},
                            ("kSignatureInvalid", "kTxInvalidSignature",
                             "not integer"))
                      for label, res in case4_malformed(base_url, args.key, pub_a,
                                                        to_hex, base_nonce + 103,
                                                        args.amount)]

    if want(5) and not args.skip_replay:
        print("\n[用例 5] nonce 重放：合法交易上链后重复提交 3 次")
        try:
            txh, receipt, replay_results = case5_replay(
                base_url, args.key, pub_a, to_hex, args.amount,
                wait_timeout=args.wait_timeout)
            results[5] = [
                judge(label, res, {10007},
                      ("kTxUserNonceInvalid", "nonce invalid"))
                for label, res in replay_results
            ]
        except RuntimeError as e:
            print(f"    [SKIP] {e}")
            results[5] = []

    # ── 汇总 ──
    print("\n" + "=" * 78)
    print("汇总")
    print("=" * 78)
    total = passed = 0
    for case_no in sorted(results):
        checks = results[case_no]
        if not checks:
            continue
        ok = sum(1 for c in checks if c)
        total += len(checks)
        passed += ok
        print(f"  用例 {case_no}: {ok}/{len(checks)} 通过")
    if total:
        print(f"\n  合计: {passed}/{total} 通过")
    else:
        print("  无有效用例执行")
    print("=" * 78)
    return 0 if total and passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
