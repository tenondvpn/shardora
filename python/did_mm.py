#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DID contract (mm.sol) client for shardora — registerDID (write) and queryDID (view).

Contract: 7635433bbc9edb01939555bd79719169986380a1
Key:      2b44b32b3ec1bc932c6dab70bd12dc681f9b406404b31bd5036c2be62107f807
Node:     http://47.111.109.8:8080/api/shard3/{...}

Self-contained on purpose: the node's signing scheme is not standard Ethereum
(no EIP-155, no RLP — a flat keccak over a fixed field layout plus recoverable
secp256k1), and no venv on this machine has coincurve/pysha3 installed. So the
signer and keccak are implemented here in pure Python rather than as a
dependency. The layout below is the one _sign_message in shardora_api.py builds;
if the node changes it, this is the one place to change.

Usage:
  python did_mm.py register --did did:example:1 --user-type person \
      --pk1 pk1value --pk2 pk2value --sig sigvalue [--amount 0] [--prefund 999999999]
  python did_mm.py query --did did:example:1
  python did_mm.py exists --did did:example:1
  python did_mm.py count
  python did_mm.py info
"""
import argparse
import json
import sys
import time
from typing import Any, Optional

import requests

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

NODE_BASE = "http://47.111.109.8:8080/api/shard{shard}/{path}"
SHARD = 3
ROOT_SHARD = 2          # root congress: runs no transaction consensus
CONTRACT = "7635433bbc9edb01939555bd79719169986380a1"
PRIVATE_KEY = "2b44b32b3ec1bc932c6dab70bd12dc681f9b406404b31bd5036c2be62107f807"

STEP_CALL_CONTRACT = 8  # kContractExcute
DEFAULT_GAS_LIMIT = 999999
DEFAULT_PREFUND = 999999999

# ─────────────────────────────────────────────────────────────────────────────
# keccak-256 (pure Python)
# ─────────────────────────────────────────────────────────────────────────────

_KECCAK_RC = [
    0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
    0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
    0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
    0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
    0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008,
]
_KECCAK_ROT = [
    [0, 36, 3, 41, 18], [1, 44, 10, 45, 2], [62, 6, 43, 15, 61],
    [28, 55, 25, 21, 56], [27, 20, 39, 8, 14],
]
_MASK = (1 << 64) - 1


def _rotl64(x: int, n: int) -> int:
    n %= 64
    return ((x << n) | (x >> (64 - n))) & _MASK if n else x


def _keccak_f1600(state: list) -> None:
    for rnd in range(24):
        c = [state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20]
             for x in range(5)]
        d = [c[(x - 1) % 5] ^ _rotl64(c[(x + 1) % 5], 1) for x in range(5)]
        for x in range(5):
            for y in range(5):
                state[x + 5 * y] ^= d[x]

        b = [0] * 25
        for x in range(5):
            for y in range(5):
                b[y + 5 * ((2 * x + 3 * y) % 5)] = _rotl64(state[x + 5 * y], _KECCAK_ROT[x][y])

        for x in range(5):
            for y in range(5):
                state[x + 5 * y] = b[x + 5 * y] ^ (
                    (~b[(x + 1) % 5 + 5 * y] & _MASK) & b[(x + 2) % 5 + 5 * y]
                )

        state[0] ^= _KECCAK_RC[rnd]


def keccak256(data: bytes) -> bytes:
    """Keccak-256 (the pre-NIST padding Ethereum uses), 32-byte digest."""
    rate = 136
    # keccak pad10*1 with 0x01 domain byte
    padded = bytearray(data) + b"\x01"
    while len(padded) % rate != 0:
        padded.append(0x00)
    padded[-1] ^= 0x80

    state = [0] * 25
    for off in range(0, len(padded), rate):
        block = padded[off:off + rate]
        for i in range(rate // 8):
            state[i] ^= int.from_bytes(block[i * 8:i * 8 + 8], "little")
        _keccak_f1600(state)

    out = bytearray()
    while len(out) < 32:
        for i in range(rate // 8):
            out += state[i].to_bytes(8, "little")
            if len(out) >= 32:
                break
        if len(out) < 32:
            _keccak_f1600(state)
    return bytes(out[:32])


# ─────────────────────────────────────────────────────────────────────────────
# secp256k1 — key derivation and recoverable signing (pure Python)
# ─────────────────────────────────────────────────────────────────────────────

_P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
_G = (
    0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798,
    0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8,
)


def _inv(a: int, m: int) -> int:
    return pow(a, m - 2, m)


def _ec_add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    if p[0] == q[0] and (p[1] + q[1]) % _P == 0:
        return None
    if p == q:
        lam = 3 * p[0] * p[0] % _P * _inv(2 * p[1], _P) % _P
    else:
        lam = (q[1] - p[1]) % _P * _inv((q[0] - p[0]) % _P, _P) % _P
    x = (lam * lam - p[0] - q[0]) % _P
    return x, (lam * (p[0] - x) - p[1]) % _P


def _ec_mul(k: int, pt=_G):
    result = None
    addend = pt
    while k:
        if k & 1:
            result = _ec_add(result, addend)
        addend = _ec_add(addend, addend)
        k >>= 1
    return result


def private_to_public(priv_int: int) -> bytes:
    """65-byte uncompressed public key: 0x04 || X || Y."""
    x, y = _ec_mul(priv_int)
    return b"\x04" + x.to_bytes(32, "big") + y.to_bytes(32, "big")


def address_from_public(pub65: bytes) -> str:
    """Account id: last 20 bytes of keccak256(pubkey without the 0x04 prefix)."""
    return keccak256(pub65[1:]).hex()[-40:]


def sign_recoverable(priv_int: int, msg32: bytes) -> tuple:
    """Deterministic recoverable ECDSA over a 32-byte digest. Returns (r, s, v).

    v is the raw recovery id (0..3) with the low bit flipped when s is
    normalised to the lower half — the node recovers the signer from v, so it
    must match the s actually sent.
    """
    z = int.from_bytes(msg32, "big")
    k = int.from_bytes(keccak256(priv_int.to_bytes(32, "big") + msg32 + b"\x00"), "big") % _N
    if k == 0:
        k = 1
    for _ in range(1024):
        r_point = _ec_mul(k)
        if r_point is not None:
            r = r_point[0] % _N
            if r != 0:
                s = _inv(k, _N) * (z + r * priv_int) % _N
                if s != 0:
                    recid = (r_point[1] & 1) | (2 if r_point[0] >= _N else 0)
                    if s > _N // 2:
                        s = _N - s
                        recid ^= 1
                    return r, s, recid
        k = int.from_bytes(keccak256(k.to_bytes(32, "big") + msg32), "big") % _N
        if k == 0:
            k = 1
    raise RuntimeError("failed to find a valid nonce for signing")


# ─────────────────────────────────────────────────────────────────────────────
# ABI encoding / decoding
# ─────────────────────────────────────────────────────────────────────────────

def _pad32(b: bytes) -> bytes:
    return b + b"\x00" * ((32 - len(b) % 32) % 32)


def selector(signature: str) -> bytes:
    return keccak256(signature.encode("utf-8"))[:4]


def encode_string(s: str) -> bytes:
    """ABI tail for one `string`: length word + utf-8 bytes, right-padded."""
    data = s.encode("utf-8")
    return len(data).to_bytes(32, "big") + _pad32(data)


def encode_uint(n: int, bits: int = 256) -> bytes:
    return n.to_bytes(bits // 8, "big")


def encode_strings_dynamic(args: list) -> bytes:
    """Encode N strings as dynamic args: N offset words, then the tails."""
    n = len(args)
    tails = [encode_string(a) for a in args]
    head = bytearray()
    offset = 32 * n
    for t in tails:
        head += offset.to_bytes(32, "big")
        offset += len(t)
    return bytes(head) + b"".join(tails)


class AbiReader:
    """Minimal reader for the return shapes mm.sol actually produces."""

    def __init__(self, raw: bytes):
        self.raw = raw

    def word(self, off: int) -> bytes:
        return self.raw[off:off + 32]

    def uint(self, off: int) -> int:
        return int.from_bytes(self.word(off), "big")

    def bytes_at_tail(self, tail_off: int) -> bytes:
        """The raw payload of a [length][bytes] pair, undecoded.

        Kept separate from the text form because `uint2str` fields are binary:
        decoding them with errors='replace' rewrites the non-UTF-8 value bytes
        to U+FFFD and the integer is unrecoverable afterwards.
        """
        length = self.uint(tail_off)
        start = tail_off + 32
        return self.raw[start:start + length]

    def read_string_at_tail(self, tail_off: int) -> str:
        return self.bytes_at_tail(tail_off).decode("utf-8", errors="replace")

    def read_string_dynamic(self, head_off: int) -> str:
        """Read a `string` whose offset word sits at head_off (single hop)."""
        return self.read_string_at_tail(head_off + self.uint(head_off))


def decode_uint2str(payload: bytes) -> Optional[int]:
    """Recover the integer behind mm.sol's `uint2str`.

    It is implemented as `string(u256ToBytes(_i))` — the value's 32-byte memory
    image reinterpreted as a Solidity string — so the field arrives as binary,
    not decimal digits. This takes the undecoded payload: a value of that shape
    is usually invalid UTF-8, and decoding it first (even with
    errors='surrogateescape') rewrites the bytes to U+FFFD and destroys the
    number.

    The image is big-endian, as the live contract shows: `created` carries
    000...01a0d289abde, whose big-endian reading is 1790238632926 —
    2026-09-24 in milliseconds, consistent with this chain — versus a
    nonsensical magnitude the other way round.
    """
    if not payload:
        return None
    return int.from_bytes(payload, "big")


def decode_query_did(raw: bytes) -> dict:
    """Decode queryDID's return:
        (string userType, string created, string updated,
         string publicKey1, string publicKey2, string signatureValue,
         AssetData[] assets)   where AssetData = (string assetId, uint8 rightType)

    The declared offsets are absolute byte positions, and each destination is
    a [length][bytes] pair. Verified against the live contract for all seven
    fields (D = declared offset, values in bytes):

        slot  field           D    length  payload
        0     userType      224     64     256
        1     created       320     32     352
        2     updated       384     32     416
        3     publicKey1    448     64     480
        4     publicKey2    544     64     576
        5     signatureValue 640    64     672
        6     assets        736      0     768

    So the offsets do not follow the standard `head_size + relative` rule this
    decoder first assumed: slot 0 names 224, which is the end of the head, and
    the following slots name positions 32 apart while their payloads are 64
    bytes — the gap exists because a 64-byte payload would otherwise start
    mid-word. A standard decoder measures from byte 0 and so reads word 7 (a
    length of 0x40) as an offset, which is why every value came back shifted
    one slot and padded with the next field's length word.
    """
    rd = AbiReader(raw)
    out = {
        "userType": rd.read_string_at_tail(rd.uint(0 * 32)),
        "created": rd.bytes_at_tail(rd.uint(1 * 32)),
        "updated": rd.bytes_at_tail(rd.uint(2 * 32)),
        "publicKey1": rd.read_string_at_tail(rd.uint(3 * 32)),
        "publicKey2": rd.read_string_at_tail(rd.uint(4 * 32)),
        "signatureValue": rd.read_string_at_tail(rd.uint(5 * 32)),
    }

    # assets: same absolute-offset convention pointing at the array, then a
    # standard dynamic array — a count word, one offset word per element
    # (relative to the first element's position), then the elements.
    #
    # AssetData is a *dynamic* struct, so an element is not laid out as
    # [string][uint8]. Its leading word is the offset of the string's length
    # word relative to the element start; the uint8 follows that word (a uint8
    # still occupies a full 32-byte slot). So:
    #   assetId   -> tail at (elem + word(elem))
    #   rightType -> word at (elem + 32)
    arr_off = rd.uint(6 * 32)
    count = rd.uint(arr_off)
    assets = []
    elem_base = arr_off + 32
    for i in range(count):
        elem = elem_base + rd.uint(elem_base + i * 32)
        assets.append({
            "assetId": rd.read_string_at_tail(elem + rd.uint(elem)),
            "rightType": rd.uint(elem + 32),
        })
    if assets:
        # Only a populated array can distinguish this from the naive layout.
        elem = elem_base + rd.uint(elem_base)
        if not assets[0]["assetId"] and rd.uint(elem) == 0x40:
            raise ValueError("AssetData 解析疑似错位：assetId 为空但首字是标准的 0x40")
    out["assets"] = assets

    # created/updated are uint2str outputs. Surface the integer and a rendered
    # timestamp, keeping the raw binary under `<field>Bytes` for callers that
    # need the original rather than the interpretation.
    for key in ("created", "updated"):
        payload = out[key]
        ts = decode_uint2str(payload)
        out[key + "Bytes"] = payload.hex()
        out[key + "Raw"] = ts
        if ts is not None and 1_000_000_000 <= ts <= 99_999_999_999:
            out[key] = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(ts))
            out[key + "Unit"] = "seconds"
        elif ts is not None and 1_000_000_000_000 <= ts <= 99_999_999_999_999:
            out[key] = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(ts / 1000))
            out[key + "Unit"] = "milliseconds"
        else:
            out[key] = None
    return out


def decode_evm_error(raw_text: str) -> Optional[str]:
    """Decode the node's "0x"-prefixed ABI `string` error envelope.

    http_handler.cc EncodeEvmError writes [offset=0x20][length][utf-8 bytes] and
    prefixes it with "0x". A successful call comes back as bare hex with no
    prefix, so the prefix alone tells the two apart.
    """
    try:
        hexs = raw_text[2:] if raw_text[:2].lower() == "0x" else raw_text
        raw = bytes.fromhex(hexs)
        rd = AbiReader(raw)
        return rd.read_string_dynamic(0)
    except Exception:
        return None


# ─────────────────────────────────────────────────────────────────────────────
# Node HTTP client
# ─────────────────────────────────────────────────────────────────────────────

def _post(shard: int, path: str, data: dict, timeout: int = 30) -> str:
    """Every endpoint on this node is form-urlencoded and answers text/plain."""
    url = NODE_BASE.format(shard=shard, path=path)
    resp = requests.post(
        url, data=data, timeout=timeout,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    resp.raise_for_status()
    return resp.text


def query_account(address: str, shard: int = SHARD) -> Optional[dict]:
    text = _post(shard, "query_account", {"address": address})
    try:
        obj = json.loads(text)
    except (ValueError, TypeError):
        return None
    return obj if isinstance(obj, dict) and "balance" in obj else None


# ─────────────────────────────────────────────────────────────────────────────
# Transaction signing / submission
# ─────────────────────────────────────────────────────────────────────────────

def _long_to_bytes(n: int) -> bytes:
    return n.to_bytes(8, "little")


def build_signable(nonce, pubkey65, to_hex, amount, gas_limit, gas_price, step,
                   contract_bytes_hex, input_hex, prefund, key, val) -> bytes:
    """The exact byte layout the node hashes and verifies.

    Nonce, amount, gas_limit, gas_price, step and prepay are 8-byte little
    endian; `to` is raw address bytes; contract code and calldata are appended
    as raw bytes only when non-empty. `key`/`val` trail as utf-8, `val` only
    when `key` is present. Response-schema-wise this mirrors _sign_message in
    shardora_api.py byte for byte.
    """
    buf = bytearray()
    buf += _long_to_bytes(nonce)
    buf += pubkey65
    buf += bytes.fromhex(to_hex)
    buf += _long_to_bytes(amount)
    buf += _long_to_bytes(gas_limit)
    buf += _long_to_bytes(gas_price)
    buf += _long_to_bytes(step)
    if contract_bytes_hex:
        buf += bytes.fromhex(contract_bytes_hex)
    if input_hex:
        buf += bytes.fromhex(input_hex)
    buf += _long_to_bytes(prefund)
    if key:
        buf += key.encode("utf-8")
        if val:
            buf += val.encode("utf-8")
    return bytes(buf)


def sign_tx(priv_int, pubkey65, nonce, to_hex, amount, gas_limit, gas_price, step,
            contract_bytes_hex="", input_hex="", prefund=0, key="", val=""):
    digest = keccak256(build_signable(
        nonce, pubkey65, to_hex, amount, gas_limit, gas_price, step,
        contract_bytes_hex, input_hex, prefund, key, val,
    ))
    r, s, v = sign_recoverable(priv_int, digest)
    return {"sign_r": f"{r:064x}", "sign_s": f"{s:064x}", "sign_v": v}, digest


def prefund_address(contract_hex: str, account_hex: str) -> str:
    """kContractExcute charges gas to `to || from` — a synthetic 40-byte account."""
    return contract_hex[-40:].rjust(40, "0") + account_hex[-40:].rjust(40, "0")


def call_contract(priv_int, pubkey65, account_hex, contract_hex, calldata_hex,
                  amount=0, gas_limit=DEFAULT_GAS_LIMIT, prefund=DEFAULT_PREFUND,
                  shard=SHARD, wait_nonce=True) -> dict:
    """Submit a step-8 (kContractExcute) transaction.

    The nonce belongs to the prefund account, not the caller and not the
    contract: contract_call.cc copies the tx nonce onto `to || from`, and
    tx_pool_manager.cc validates it against that same synthetic address. A
    freshly created contract has prefund nonce 0, so the first call must be 1.
    """
    pf = prefund_address(contract_hex, account_hex)

    info = query_account(pf, shard)
    if info is None and wait_nonce:
        # The prefund row only exists once the creating (or a previous) tx has
        # committed, so a miss right after deploy is expected — poll rather than
        # guessing a nonce, because a wrong nonce is rejected outright.
        for _ in range(10):
            time.sleep(3)
            info = query_account(pf, shard)
            if info is not None:
                break

    if info is None:
        return {
            "ok": False,
            "msg": f"prefund 账户 {pf} 不存在，合约未部署或尚未提交过交易；无法确定 nonce",
        }

    nonce = int(info["nonce"]) + 1
    params, digest = sign_tx(
        priv_int, pubkey65, nonce, contract_hex, amount, gas_limit, 1,
        STEP_CALL_CONTRACT, "", calldata_hex, prefund, "", "",
    )
    payload = {
        "nonce": nonce,
        "pubkey": pubkey65.hex(),
        "to": contract_hex,
        "type": STEP_CALL_CONTRACT,
        "amount": amount,
        "gas_limit": gas_limit,
        "gas_price": 1,
        "shard_id": shard,
        "prefund": prefund,
        "sign_r": params["sign_r"],
        "sign_s": params["sign_s"],
        "sign_v": params["sign_v"],
    }
    if calldata_hex:
        payload["input"] = calldata_hex

    text = _post(shard, "transaction", payload)
    ok = text.strip() == "ok"
    return {
        "ok": ok, "msg": text.strip(), "nonce": nonce,
        "prefund": pf, "txHash": digest.hex(), "raw": text,
    }


# ─────────────────────────────────────────────────────────────────────────────
# Contract calls
# ─────────────────────────────────────────────────────────────────────────────

def abi_query_contract(contract_hex: str, calldata_hex: str, from_hex: str,
                       shard: int = SHARD) -> dict:
    """Read-only call. Success is bare hex; failure is a "0x"-prefixed envelope."""
    text = _post(shard, "abi_query_contract", {
        "address": contract_hex, "input": calldata_hex, "from": from_hex,
    }).strip()

    if text.startswith("0x") or text.startswith("0X"):
        return {"ok": False, "msg": decode_evm_error(text) or "合约调用失败", "raw": text}
    return {"ok": True, "outputHex": text}


def register_did(priv_int, pubkey65, account_hex, did: str, user_type: str,
                 pk1: str, pk2: str, sig_value: str, amount: int = 0,
                 prefund: int = DEFAULT_PREFUND, shard: int = SHARD) -> dict:
    """registerDID(string,string,string,string,string) — five dynamic strings."""
    args = [did, user_type, pk1, pk2, sig_value]
    calldata = (selector("registerDID(string,string,string,string,string)")
                + encode_strings_dynamic(args)).hex()
    return call_contract(priv_int, pubkey65, account_hex, CONTRACT, calldata,
                         amount=amount, prefund=prefund, shard=shard)


def query_did(account_hex, did: str, shard: int = SHARD) -> dict:
    """queryDID(string) — view call plus ABI decode of the 7-value return."""
    calldata = (selector("queryDID(string)") + encode_strings_dynamic([did])).hex()
    res = abi_query_contract(CONTRACT, calldata, account_hex, shard)
    if not res["ok"]:
        return res

    raw = bytes.fromhex(res["outputHex"])
    try:
        res["decoded"] = decode_query_did(raw)
    except Exception as exc:  # keep the raw hex so the failure is inspectable
        res["ok"] = False
        res["msg"] = f"ABI 解码失败: {exc}"
    return res


def exists_did(account_hex, did: str, shard: int = SHARD) -> dict:
    """existsDID(string) -> bool"""
    calldata = (selector("existsDID(string)") + encode_strings_dynamic([did])).hex()
    res = abi_query_contract(CONTRACT, calldata, account_hex, shard)
    if res["ok"]:
        res["exists"] = int(res["outputHex"], 16) != 0
    return res


def did_count(account_hex, shard: int = SHARD) -> dict:
    """getDIDCount() -> uint"""
    calldata = selector("getDIDCount()").hex()
    res = abi_query_contract(CONTRACT, calldata, account_hex, shard)
    if res["ok"]:
        res["count"] = int(res["outputHex"], 16)
    return res


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def _key():
    priv = int(PRIVATE_KEY, 16)
    pub = private_to_public(priv)
    return priv, pub, address_from_public(pub)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="mm.sol DID client")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_reg = sub.add_parser("register", help="registerDID")
    p_reg.add_argument("--did", required=True)
    p_reg.add_argument("--user-type", required=True)
    p_reg.add_argument("--pk1", required=True)
    p_reg.add_argument("--pk2", required=True)
    p_reg.add_argument("--sig", required=True, dest="sig_value")
    p_reg.add_argument("--amount", type=int, default=0)
    p_reg.add_argument("--prefund", type=int, default=DEFAULT_PREFUND)

    p_q = sub.add_parser("query", help="queryDID (ABI-decoded)")
    p_q.add_argument("--did", required=True)

    p_e = sub.add_parser("exists", help="existsDID")
    p_e.add_argument("--did", required=True)

    sub.add_parser("count", help="getDIDCount")
    sub.add_parser("info", help="show derived address and prefund state")

    args = parser.parse_args(argv)
    priv, pub, account = _key()

    if args.cmd == "info":
        pf = prefund_address(CONTRACT, account)
        print(json.dumps({
            "account": account,
            "contract": CONTRACT,
            "prefund": pf,
            "account_info": query_account(account),
            "prefund_info": query_account(pf),
        }, indent=2, ensure_ascii=False))
        return 0

    if args.cmd == "register":
        res = register_did(priv, pub, account, args.did, args.user_type,
                           args.pk1, args.pk2, args.sig_value,
                           amount=args.amount, prefund=args.prefund)
    elif args.cmd == "query":
        res = query_did(account, args.did)
    elif args.cmd == "exists":
        res = exists_did(account, args.did)
    else:
        res = did_count(account)

    print(json.dumps(res, indent=2, ensure_ascii=False))
    return 0 if res.get("ok") or res.get("exists") is not None else 1


if __name__ == "__main__":
    sys.exit(main())
