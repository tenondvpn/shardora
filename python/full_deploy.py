"""
全量部署脚本 - 链重置后一键执行所有操作:
  1. 部署 V3 Factory
  2. 部署 WSHARDORA9
  3. 部署 sUSDC
  4. sUSDC.addMinter(sender)
  5. sUSDC.mint(sender, 1_000_000 sUSDC)
  6. Factory.setParameters(token0, token1, fee)
  7. 外部部署 UniswapV3Pool
  8. Factory.registerPool(token0, token1, fee, pool)
  9. Pool.initialize(sqrtPriceX96)
 10. 验证所有状态
"""
import os, struct, hashlib, time, math, sys
import solcx
from solcx import compile_source, compile_files, install_solc
import requests
from Crypto.Hash import keccak
from ecdsa import SigningKey, SECP256k1
from ecdsa.util import sigencode_string_canonize
import eth_abi
from eth_utils import to_checksum_address
from shardora_sdk import ShardoraWeb3Mock, StepType, compile_and_link, get_sm2_public_key

HOST        = os.getenv("SHARDORA_HOST",    "35.197.170.240")
PORT        = int(os.getenv("SHARDORA_PORT", "23001"))
PRIVATE_KEY = "4b6525236a2029ab54e2c6162c483133c1af7d38bd960f85b1f485c31e696b7b"

FEE               = 3000
MINT_AMOUNT       = 1_000_000 * 10**6   # 1,000,000 sUSDC (6 decimals)
INITIAL_PRICE_RAW = (10**18) / (2000 * 10**6)  # 1 WSHARDORA9 = 2000 sUSDC

BASE_URL = f"http://{HOST}:{PORT}"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ---------- helpers ----------

def _u64(v): return struct.pack("<Q", v)
def _hb(s):  return bytes.fromhex(s.strip().replace("0x", ""))

def get_address(pk):
    sk = SigningKey.from_string(bytes.fromhex(pk.replace("0x","")), curve=SECP256k1)
    pub = sk.verifying_key.to_string("uncompressed")[1:]
    k = keccak.new(digest_bits=256); k.update(pub)
    return k.digest()[-20:].hex()

def get_nonce(addr):
    try:
        r = requests.post(f"{BASE_URL}/query_account", data={"address": addr}, timeout=5)
        return int(r.json().get("nonce", 0)) if r.status_code == 200 else 0
    except: return 0

def get_balance(addr):
    try:
        r = requests.post(f"{BASE_URL}/query_account", data={"address": addr}, timeout=5)
        return int(r.json().get("balance", 0)) if r.status_code == 200 else 0
    except: return 0

def get_selector(sig):
    k = keccak.new(digest_bits=256); k.update(sig.encode())
    return k.digest()[:4].hex()

def query_contract(sender, contract_addr, input_hex):
    try:
        r = requests.post(f"{BASE_URL}/query_contract",
                          data={"from": sender, "address": contract_addr, "input": input_hex}, timeout=5)
        return r.text if r.status_code == 200 else None
    except: return None

def calc_create2(deployer, salt_hex, bytecode_hex):
    deployer_b = _hb(deployer)
    salt_b     = bytes.fromhex(salt_hex.replace("0x","").zfill(64))
    code_b     = _hb(bytecode_hex)
    kc = keccak.new(digest_bits=256); kc.update(code_b)
    kf = keccak.new(digest_bits=256)
    kf.update(b"\xff" + deployer_b + salt_b + kc.digest())
    return kf.digest()[-20:].hex().lower()

def _build_tx(pk, to, step, bytecode_hex="", input_hex="", prepayment=0, gas_limit=10_000_000):
    pk = pk.replace("0x","")
    sk = SigningKey.from_string(bytes.fromhex(pk), curve=SECP256k1)
    pubkey = sk.verifying_key.to_string("uncompressed").hex()
    sender = get_address(pk)

    if step == 8:
        nonce_addr = to + sender
    else:
        nonce_addr = sender
    nonce = get_nonce(nonce_addr) + 1

    msg = bytearray()
    msg.extend(_u64(nonce)); msg.extend(_hb(pubkey)); msg.extend(_hb(to))
    msg.extend(_u64(0)); msg.extend(_u64(gas_limit)); msg.extend(_u64(1))
    msg.extend(_u64(step))
    if bytecode_hex: msg.extend(_hb(bytecode_hex))
    if input_hex:    msg.extend(_hb(input_hex))
    if prepayment:   msg.extend(_u64(prepayment))
    kh = keccak.new(digest_bits=256); kh.update(msg)
    tx_hash = kh.digest()

    sig = sk.sign_digest_deterministic(tx_hash, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
    data = {
        "nonce": str(nonce), "pubkey": pubkey, "to": to, "amount": "0",
        "gas_limit": str(gas_limit), "gas_price": "1", "shard_id": "0",
        "type": str(step),
        "sign_r": sig[:32].hex(), "sign_s": sig[32:].hex(), "sign_v": "0",
    }
    if bytecode_hex: data["bytes_code"] = bytecode_hex
    if input_hex:    data["input"] = input_hex
    if prepayment:   data["prefund"] = str(prepayment)
    return tx_hash.hex(), data

def send_tx(pk, to, step, bytecode_hex="", input_hex="", prepayment=10_000_000, gas_limit=10_000_000):
    tx_hash, data = _build_tx(pk, to, step, bytecode_hex, input_hex, prepayment, gas_limit)
    r = requests.post(f"{BASE_URL}/transaction", data=data, timeout=10)
    print(f"    [TX] {r.text}")
    if "SignatureInvalid" in r.text or "kSignatureInvalid" in r.text:
        data["sign_v"] = "1"
        r = requests.post(f"{BASE_URL}/transaction", data=data, timeout=10)
        print(f"    [TX retry] {r.text}")
    return tx_hash

def wait_receipt(tx_hash, timeout=90):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = requests.post(f"{BASE_URL}/transaction_receipt", data={"tx_hash": tx_hash}, timeout=5)
            if r.status_code == 200:
                obj = r.json()
                status = int(obj.get("status", -1))
                print(f"    [Receipt] {status} {obj.get('msg','')}")
                if status not in (10001, 10003):
                    return status in (0, 5011)
        except: pass
        time.sleep(1)
    print("    [Receipt] timeout"); return False

def deploy_contract(label, salt, bytecode, prepayment=20_000_000):
    sender = get_address(PRIVATE_KEY)
    addr = calc_create2(sender, salt, bytecode)
    print(f"  {label}: {addr}")
    tx = send_tx(PRIVATE_KEY, addr, step=6, bytecode_hex=bytecode, prepayment=prepayment)
    ok = wait_receipt(tx)
    print(f"  {label} deploy: {'OK' if ok else 'FAIL'}")
    return addr, ok

def call_contract(label, contract_addr, fn_sig, types=None, vals=None, prepayment=10_000_000):
    sel = get_selector(fn_sig)
    inp = sel
    if types and vals:
        inp += eth_abi.encode(types, vals).hex()
    tx = send_tx(PRIVATE_KEY, contract_addr, step=8, input_hex=inp, prepayment=prepayment)
    ok = wait_receipt(tx)
    print(f"  {label}: {'OK' if ok else 'FAIL'}")
    return ok

def sqrt_price_x96(price):
    return int(math.sqrt(price) * (2 ** 96))

# ---------- compile ----------

def compile_all():
    print("[Compile] Installing solc 0.8.20...")
    install_solc("0.8.20")
    solcx.set_solc_version("0.8.20")

    contracts_root = os.path.join(SCRIPT_DIR, "v3-core-0.8", "contracts")
    compiled = compile_files(
        [os.path.join(contracts_root, "UniswapV3Factory.sol"),
         os.path.join(contracts_root, "UniswapV3Pool.sol")],
        output_values=["abi", "bin"],
        solc_version="0.8.20",
        optimize=True, optimize_runs=200,
        allow_paths=contracts_root,
    )
    factory_key = next(k for k in compiled if "UniswapV3Factory" in k and "Pool" not in k)
    pool_key    = next(k for k in compiled if "UniswapV3Pool" in k and "Deployer" not in k)

    # WSHARDORA9
    with open(os.path.join(SCRIPT_DIR, "weth9_v8.sol"), encoding="utf-8") as f:
        wshardora9_src = f.read()
    wshardora9_compiled = compile_source(wshardora9_src, output_values=["abi","bin"],
                                     solc_version="0.8.20", optimize=True, optimize_runs=200)
    wshardora9_contract = list(wshardora9_compiled.values())[0]

    # sUSDC
    with open(os.path.join(SCRIPT_DIR, "sUSDC.sol"), encoding="utf-8") as f:
        susdc_src = f.read()
    susdc_compiled = compile_source(susdc_src, output_values=["abi","bin"],
                                    solc_version="0.8.20", optimize=True, optimize_runs=200)
    susdc_contract = list(susdc_compiled.values())[0]

    print(f"  Factory : {len(compiled[factory_key]['bin'])//2} bytes")
    print(f"  Pool    : {len(compiled[pool_key]['bin'])//2} bytes")
    print(f"  WSHARDORA9  : {len(wshardora9_contract['bin'])//2} bytes")
    print(f"  sUSDC   : {len(susdc_contract['bin'])//2} bytes")

    return (compiled[factory_key]["bin"].strip().replace("0x",""),
            compiled[pool_key]["bin"].strip().replace("0x",""),
            wshardora9_contract["bin"].strip().replace("0x",""),
            susdc_contract["bin"].strip().replace("0x",""))

# ---------- main ----------

if __name__ == "__main__":
    sender = get_address(PRIVATE_KEY)
    print("=" * 60)
    print("Full Deploy Script")
    print("=" * 60)
    print(f"Sender  : {sender}")
    print(f"Balance : {get_balance(sender)}")
    print()

    factory_bin, pool_bin, wshardora9_bin, susdc_bin = compile_all()
    print()

    # 1. Deploy Factory
    print("[1] Deploy V3 Factory...")
    factory_addr, ok = deploy_contract("Factory", "11", factory_bin)
    if not ok: exit(1)

    # 2. Deploy WSHARDORA9
    print("\n[2] Deploy WSHARDORA9...")
    wshardora9_addr, ok = deploy_contract("WSHARDORA9", "12", wshardora9_bin)
    if not ok: exit(1)

    # 3. Deploy sUSDC
    print("\n[3] Deploy sUSDC...")
    susdc_addr, ok = deploy_contract("sUSDC", "13", susdc_bin)
    if not ok: exit(1)

    # 4. addMinter
    print("\n[4] sUSDC.addMinter(sender)...")
    call_contract("addMinter", susdc_addr, "addMinter(address)",
                  ["address"], [to_checksum_address("0x" + sender)])

    # 5. mint sUSDC
    print(f"\n[5] sUSDC.mint(sender, {MINT_AMOUNT})...")
    call_contract("mint", susdc_addr, "mint(address,uint256)",
                  ["address", "uint256"], [to_checksum_address("0x" + sender), MINT_AMOUNT])

    # token sort
    token0 = susdc_addr if int(susdc_addr,16) < int(wshardora9_addr,16) else wshardora9_addr
    token1 = wshardora9_addr if token0 == susdc_addr else susdc_addr
    print(f"\n  token0: {token0}  ({'sUSDC' if token0==susdc_addr else 'WSHARDORA9'})")
    print(f"  token1: {token1}  ({'WSHARDORA9' if token1==wshardora9_addr else 'sUSDC'})")

    # 6. createPool
    print("\n[6] Factory.createPool(token0, token1, fee)...")
    call_contract("createPool", factory_addr, "createPool(address,address,uint24)",
                  ["address","address","uint24"],
                  [to_checksum_address("0x"+token0), to_checksum_address("0x"+token1), FEE],
                  prepayment=30_000_000)

    # 查询 pool 地址
    sel = get_selector("getPool(address,address,uint24)")
    raw = query_contract(sender, factory_addr,
                         sel + eth_abi.encode(["address","address","uint24"],
                         [to_checksum_address("0x"+token0), to_checksum_address("0x"+token1), FEE]).hex())
    pool_addr = raw.strip().lower().replace("0x","")[-40:] if raw else None
    print(f"  Pool addr: {pool_addr}")


    w3 = ShardoraWeb3Mock(HOST, PORT)
    MY = w3.client.get_address(PRIVATE_KEY)
    contract = w3.shardora.contract(address=pool_addr, sender_address=MY)
    initial = contract.get_prefund(MY)
    print(f"Initial Prefund: {initial}")
    deposit_amount = 5000000
    print(f"Action: Depositing {deposit_amount} to prefund...")
    
    # Call the prefund interface from the contract object
    receipt = contract.prefund(deposit_amount, PRIVATE_KEY) # Use the contract object's prefund method
    
    if receipt.get('status') == 0:
        print("✅ Prefund Tx success.")
    else:
        print(f"❌ Prefund Tx failed: {receipt.get('msg')}")
        sys.exit(1)

    # ---------------------------------------------------------
    count = 0
    while count < 30:
        time.sleep(2) # Wait for consensus to settle
        after_deposit = contract.get_prefund(MY)
        print(f"Prefund after deposit: {after_deposit}")
        
        if after_deposit == initial + deposit_amount:
            print("🚩 Verification 1: Accumulation SUCCESS!")
            break
        else:
            count += 1
            print("🚩 Verification 1: Accumulation FAILED!")

    if count == 30:
        print(f"❌ Prefund Tx failed: {after_deposit.get('msg')}")
        sys.exit(1)

    # 7. initialize
    print("\n[7] Pool.initialize(sqrtPriceX96)...")
    sqrtP = sqrt_price_x96(INITIAL_PRICE_RAW)
    print(f"  sqrtPriceX96: {sqrtP}")
    call_contract("initialize", pool_addr, "initialize(uint160)",
                  ["uint160"], [sqrtP])

    # 8. verify
    print("\n[8] Verify...")
    # balanceOf sUSDC
    sel = get_selector("balanceOf(address)")
    raw = query_contract(sender, susdc_addr, sel + eth_abi.encode(["address"],[to_checksum_address("0x"+sender)]).hex())
    if raw:
        bal = int(raw.strip().lower().replace("0x","")[-64:], 16)
        print(f"  sUSDC balance : {bal / 10**6:,.0f} sUSDC")

    # getPool
    sel = get_selector("getPool(address,address,uint24)")
    raw = query_contract(sender, factory_addr,
                         sel + eth_abi.encode(["address","address","uint24"],
                         [to_checksum_address("0x"+token0), to_checksum_address("0x"+token1), FEE]).hex())
    registered = raw.strip().lower().replace("0x","")[-40:] if raw else "?"
    print(f"  getPool       : {registered}")
    print(f"  expected      : {pool_addr}")
    print(f"  match         : {registered == pool_addr}")

    # slot0
    raw = query_contract(sender, pool_addr, get_selector("slot0()"))
    print(f"get slot0: {raw}")
    if raw and len(raw.strip().replace("0x","")) >= 64:
        stored = int(raw.strip().lower().replace("0x","")[:64], 16)
        price  = (stored / 2**96) ** 2
        print(f"  slot0 price   : {price:.4e} (token1/token0)")

    print("\n" + "=" * 60)
    print(f"Factory : {factory_addr}")
    print(f"WSHARDORA9  : {wshardora9_addr}")
    print(f"sUSDC   : {susdc_addr}")
    print(f"Pool    : {pool_addr}")
    print(f"token0  : {token0}")
    print(f"token1  : {token1}")
    print(f"fee     : {FEE}")
    print("=" * 60)
