"""要素匹配合约测试公共库：编译、部署、发交易、查询、测试数据。"""
import os, sys, re, json, struct, hashlib, time, configparser
import requests, eth_abi, solcx
from Crypto.Hash import keccak
from ecdsa import SigningKey, SECP256k1
from ecdsa.util import sigencode_string_canonize

HERE = os.path.dirname(os.path.abspath(__file__))

SCRIPT_DIR = os.path.dirname(os.path.abspath(sys.argv[0])) if sys.argv and sys.argv[0] else os.getcwd()
CONF_PATH = None

def _load_conf():
    """读取 run.conf 的 [run] 段。查找顺序: RUN_CONF 环境变量 > 当前目录 > 入口脚本所在目录"""
    global CONF_PATH
    for p in (os.getenv("RUN_CONF"), os.path.join(os.getcwd(), "run.conf"), os.path.join(SCRIPT_DIR, "run.conf")):
        if p and os.path.isfile(p):
            CONF_PATH = os.path.abspath(p)
            cp = configparser.ConfigParser(inline_comment_prefixes=("#",))
            cp.read(p, encoding="utf-8")
            return dict(cp["run"])
    sys.exit("找不到 run.conf")
CONF = _load_conf()
def conf(k, default=None):
    v = CONF.get(k, default)
    if v is None: sys.exit(f"run.conf [run] 缺少参数: {k}")
    return v

def set_conf(key, val):
    """把 key=val 写回 run.conf(已有则替换，没有则加在 [run] 下)，并更新内存"""
    CONF[key] = val
    lines = open(CONF_PATH, encoding="utf-8").read().split("\n")
    for i, ln in enumerate(lines):
        if re.match(rf"^\s*{re.escape(key)}\s*=", ln):
            lines[i] = f"{key} = {val}"; break
    else:
        j = next(i for i, ln in enumerate(lines) if ln.strip() == "[run]")
        lines.insert(j + 1, f"{key} = {val}")
    open(CONF_PATH, "w", encoding="utf-8").write("\n".join(lines))

HOST = conf("host")
PORT = int(conf("port"))
SHARD_ID = conf("shard_id", "0")
PRIVATE_KEY = conf("private_key").replace("0x", "")
GAS_LIMIT = int(conf("gas_limit", "10000000"))
PREFUND = int(conf("prefund", "10000000"))
DEPLOY_PREFUND = int(conf("deploy_prefund", "20000000"))
RECEIPT_TIMEOUT = int(conf("receipt_timeout", "90"))
SOLC_VERSION = conf("solc_version", "0.8.20")
BASE_TIME = int(conf("base_time", "1800000000"))
SCHEME = "https"       # 写死
VERIFY_TLS = False     # 写死: 不校验证书
BASE = f"{SCHEME}://{HOST}:{PORT}"
if not VERIFY_TLS:
    import urllib3; urllib3.disable_warnings()
def post(url, **kw):
    kw.setdefault("verify", VERIFY_TLS)
    return requests.post(url, **kw)
SOL = os.path.join(HERE, "..", "..", "contracts", "FactorMatching.sol")

# ABI 类型
T_DREQ = "(bytes32,bytes32,bytes32,uint64,uint32,uint256)"
T_MREQ = "(bytes32,bytes32,uint32,uint32,uint256)"
T_CREQ = "(uint32,uint32,uint32,uint64,uint64,uint256)"
T_TASK = f"(address,{T_DREQ},{T_MREQ},{T_CREQ},uint256,uint256,uint256,uint256,bytes32,uint8)"
T_REC = "(uint8,uint8,uint256,address,uint256,uint64)"

def b32(s): return s.encode().ljust(32, b"\0")
def u64(v): return struct.pack("<Q", v)
def hb(s): return bytes.fromhex(s.strip().replace("0x", ""))
def kec(b):
    k = keccak.new(digest_bits=256); k.update(b); return k.digest()

def addr_of(pk):
    sk = SigningKey.from_string(bytes.fromhex(pk), curve=SECP256k1)
    return kec(sk.verifying_key.to_string("uncompressed")[1:])[-20:].hex()
SENDER = addr_of(PRIVATE_KEY)

def selector(sig): return kec(sig.encode())[:4].hex()

def get_nonce(a):
    try:
        r = post(f"{BASE}/query_account", data={"address": a}, timeout=5)
        return int(r.json().get("nonce", 0)) if r.status_code == 200 else 0
    except Exception: return 0

def build_tx(to, step, bytecode="", inp="", prefund=None, gas=None):
    prefund = PREFUND if prefund is None else prefund
    gas = GAS_LIMIT if gas is None else gas
    sk = SigningKey.from_string(bytes.fromhex(PRIVATE_KEY), curve=SECP256k1)
    pub = sk.verifying_key.to_string("uncompressed").hex()
    nonce = get_nonce(to + SENDER if step == 8 else SENDER) + 1
    msg = bytearray(u64(nonce) + hb(pub) + hb(to) + u64(0) + u64(gas) + u64(1) + u64(step))
    if bytecode: msg += hb(bytecode)
    if inp: msg += hb(inp)
    if prefund: msg += u64(prefund)
    h = kec(bytes(msg))
    sig = sk.sign_digest_deterministic(h, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
    d = {"nonce": str(nonce), "pubkey": pub, "to": to, "amount": "0", "gas_limit": str(gas),
         "gas_price": "1", "shard_id": SHARD_ID, "type": str(step),
         "sign_r": sig[:32].hex(), "sign_s": sig[32:].hex(), "sign_v": "0"}
    if bytecode: d["bytes_code"] = bytecode
    if inp: d["input"] = inp
    if prefund: d["prefund"] = str(prefund)
    return h.hex(), d

def send(to, step, bytecode="", inp="", prefund=None):
    h, d = build_tx(to, step, bytecode, inp, prefund)
    r = post(f"{BASE}/transaction", data=d, timeout=10)
    if "SignatureInvalid" in r.text or "kSignatureInvalid" in r.text:
        d["sign_v"] = "1"
        r = post(f"{BASE}/transaction", data=d, timeout=10)
    return h

def wait(h, timeout=None):
    timeout = timeout or RECEIPT_TIMEOUT
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = post(f"{BASE}/transaction_receipt", data={"tx_hash": h}, timeout=5)
            if r.status_code == 200:
                o = r.json(); st = int(o.get("status", -1))
                if st not in (10001, 10003):
                    return st in (0, 5011), o
        except Exception: pass
        time.sleep(1)
    return False, {"msg": "timeout"}

def compile_contract():
    solcx.install_solc(SOLC_VERSION); solcx.set_solc_version(SOLC_VERSION)
    src = open(SOL, encoding="utf-8").read()
    out = solcx.compile_source(src, output_values=["abi", "bin"], solc_version=SOLC_VERSION,
                               optimize=True, optimize_runs=200)
    return list(out.values())[0]["bin"].replace("0x", "")

def create2(deployer, salt_hex, code_hex):
    return kec(b"\xff" + hb(deployer) + bytes.fromhex(salt_hex.zfill(64)) + kec(hb(code_hex)))[-20:].hex()

def deploy():
    code = compile_contract()
    salt = format(int(time.time()), "x")
    addr = create2(SENDER, salt, code)
    ok, o = wait(send(addr, 6, bytecode=code, prefund=DEPLOY_PREFUND))
    assert ok, f"deploy failed: {o}"
    return addr

_ADDR = None
def contract_addr():
    """run.conf 的 contract 非空则使用并校验；为空则自动部署并写回 run.conf"""
    global _ADDR
    if _ADDR: return _ADDR
    v = conf("contract", "").replace("0x", "").lower().strip()
    if v:
        _ADDR = v
        try: counts()
        except Exception as e:
            _ADDR = None
            sys.exit(f"run.conf 中的合约地址 {v} 不可用: {e}")
    else:
        print("[依赖] run.conf 未配置 contract，自动部署 FactorMatching ...")
        _ADDR = deploy()
        set_conf("contract", _ADDR)
        print(f"[依赖] 已部署并写回 run.conf: {_ADDR}")
    return _ADDR

def tx(fn_sig, types, vals, expect_ok=True):
    """发送合约交易并等待回执"""
    inp = selector(fn_sig) + eth_abi.encode(types, vals).hex()
    ok, o = wait(send(contract_addr(), 8, inp=inp))
    if expect_ok: assert ok, f"{fn_sig} failed: {o}"
    return ok

def call(fn_sig, types, vals, out_types):
    """只读查询。用 /abi_query_contract: 成功返回完整 ABI 输出的十六进制(无0x)，
    失败返回 0x 开头的 Error(string) 编码(/query_contract 只适合返回 string 的合约，不能用)"""
    inp = selector(fn_sig) + (eth_abi.encode(types, vals).hex() if types else "")
    r = post(f"{BASE}/abi_query_contract",
                      data={"from": SENDER, "address": contract_addr(), "input": inp}, timeout=10)
    txt = r.text.strip()
    if txt.startswith("0x"):   # 节点的错误返回
        try: msg = eth_abi.decode(["string"], bytes.fromhex(txt[2:]))[0]
        except Exception: msg = txt
        raise RuntimeError(f"{fn_sig} 查询失败: {msg}")
    return eth_abi.decode(out_types, bytes.fromhex(txt))

# --- 业务封装 ---
def reg_data(cat, region, fmt, size, quality, price):
    tx("registerData(bytes32,bytes32,bytes32,uint64,uint32,uint256)",
       ["bytes32"]*3 + ["uint64", "uint32", "uint256"], [b32(cat), b32(region), b32(fmt), size, quality, price])
def reg_model(tt, fw, ver, acc, params, price):
    tx("registerModel(bytes32,bytes32,bytes32,uint32,uint32,uint256)",
       ["bytes32"]*3 + ["uint32", "uint32", "uint256"], [b32(tt), b32(fw), b32(ver), acc, params, price])
def reg_compute(cpu, gpu, mem, f, t, p):
    tx("registerCompute(uint32,uint32,uint32,uint64,uint64,uint256)",
       ["uint32", "uint32", "uint32", "uint64", "uint64", "uint256"], [cpu, gpu, mem, f, t, p])

def dreq(cat="", region="", fmt="", min_size=0, min_q=0, max_price=0):
    return (b32(cat) if cat else b"\0"*32, b32(region) if region else b"\0"*32,
            b32(fmt) if fmt else b"\0"*32, min_size, min_q, max_price)
def mreq(tt="", fw="", min_acc=0, max_params=0, max_price=0):
    return (b32(tt) if tt else b"\0"*32, b32(fw) if fw else b"\0"*32, min_acc, max_params, max_price)
def creq(cpu=0, gpu=0, mem=0, start=0, end=0, max_price=0):
    return (cpu, gpu, mem, start, end, max_price)

def match_data(r):  return list(call(f"matchData({T_DREQ})", [T_DREQ], [r], ["uint256[]"])[0])
def match_model(r): return list(call(f"matchModel({T_MREQ})", [T_MREQ], [r], ["uint256[]"])[0])
def match_compute(r): return list(call(f"matchCompute({T_CREQ})", [T_CREQ], [r], ["uint256[]"])[0])

def counts(): return call("counts()", [], [], ["uint256"]*4)
def get_data(i):    return call("dataFactors(uint256)", ["uint256"], [i], ["address","bytes32","bytes32","bytes32","uint64","uint32","uint256","bool"])
def get_model(i):   return call("modelFactors(uint256)", ["uint256"], [i], ["address","bytes32","bytes32","bytes32","uint32","uint32","uint256","bool"])
def get_compute(i): return call("computeFactors(uint256)", ["uint256"], [i], ["address","uint32","uint32","uint32","uint64","uint64","uint256","bool"])

# --- 手工(链下)筛选，用于比对 ---
def manual_data(r):
    z = b"\0"*32; out = []
    for i in range(counts()[0]):
        _, c, rg, f, size, q, p, act = get_data(i)
        if act and (r[0]==z or c==r[0]) and (r[1]==z or rg==r[1]) and (r[2]==z or f==r[2]) \
           and size>=r[3] and q>=r[4] and (r[5]==0 or p<=r[5]): out.append(i)
    return out
def manual_model(r):
    z = b"\0"*32; out = []
    for i in range(counts()[1]):
        _, tt, fw, _, acc, pm, p, act = get_model(i)
        if act and (r[0]==z or tt==r[0]) and (r[1]==z or fw==r[1]) and acc>=r[2] \
           and (r[3]==0 or pm<=r[3]) and (r[4]==0 or p<=r[4]): out.append(i)
    return out

def occupied(cid):
    return call("getSlots(uint256)", ["uint256"], [cid], ["(uint64,uint64,uint256)[]"])[0]
def manual_compute(r):
    out = []
    for i in range(counts()[2]):
        _, cpu, gpu, mem, af, at, p, act = get_compute(i)
        free = all(not (r[3] < e and s < r[4]) for s, e, _ in occupied(i))
        if act and r[4]>r[3] and cpu>=r[0] and gpu>=r[1] and mem>=r[2] and af<=r[3] and at>=r[4] \
           and (r[5]==0 or p<=r[5]) and free: out.append(i)
    return out

def conf_list(prefix):
    """读取 prefix_1, prefix_2 ... 按序号排序，返回各项逗号分隔后的字段列表"""
    ks = sorted((k for k in CONF if k.startswith(prefix + "_") and k[len(prefix)+1:].isdigit()),
                key=lambda k: int(k.split("_")[-1]))
    return [[x.strip() for x in CONF[k].split(",")] for k in ks]

def _n(x): return int(x) if x else 0
def parse_dreq(key):   # category,region,format,min_size,min_quality,max_price (空=不限)
    f = (conf(key).split(",") + [""]*6)[:6]; f = [x.strip() for x in f]
    return dreq(f[0], f[1], f[2], _n(f[3]), _n(f[4]), _n(f[5]))
def parse_mreq(key):   # task_type,framework,min_accuracy,max_params,max_price
    f = (conf(key).split(",") + [""]*5)[:5]; f = [x.strip() for x in f]
    return mreq(f[0], f[1], _n(f[2]), _n(f[3]), _n(f[4]))
def parse_creq(key):   # cpu,gpu,mem,start_off,end_off,max_price  (时间为相对 base_time 的偏移秒)
    f = (conf(key).split(",") + [""]*6)[:6]; f = [x.strip() for x in f]
    return creq(_n(f[0]), _n(f[1]), _n(f[2]), BASE_TIME + _n(f[3]), BASE_TIME + _n(f[4]), _n(f[5]))

def ensure_factors(data=False, model=False, compute=False):
    """自动满足前置条件: 确保 run.conf 中 data_N/model_N/compute_N 描述的要素都已在链上，缺哪个登记哪个"""
    contract_addr()
    if data:
        used = set()
        for f in conf_list("data"):   # category,region,format,size,quality,price
            want = (b32(f[0]), b32(f[1]), b32(f[2]), int(f[3]), int(f[4]), int(f[5]))
            hit = next((i for i in range(counts()[0]) if i not in used and get_data(i)[7]
                        and get_data(i)[1:7] == want), None)
            if hit is None: reg_data(f[0], f[1], f[2], int(f[3]), int(f[4]), int(f[5])); hit = counts()[0] - 1; print("  [依赖] 登记数据要素", f)
            used.add(hit)
    if model:
        used = set()
        for f in conf_list("model"):  # task_type,framework,version,accuracy,params_m,price
            want = (b32(f[0]), b32(f[1]), b32(f[2]), int(f[3]), int(f[4]), int(f[5]))
            hit = next((i for i in range(counts()[1]) if i not in used and get_model(i)[7]
                        and get_model(i)[1:7] == want), None)
            if hit is None: reg_model(f[0], f[1], f[2], int(f[3]), int(f[4]), int(f[5])); hit = counts()[1] - 1; print("  [依赖] 登记模型要素", f)
            used.add(hit)
    if compute:
        used = set()
        for f in conf_list("compute"):  # cpu,gpu,mem,from_off,to_off,price_per_hour
            want = (int(f[0]), int(f[1]), int(f[2]), BASE_TIME + int(f[3]), BASE_TIME + int(f[4]), int(f[5]))
            hit = next((i for i in range(counts()[2]) if i not in used and get_compute(i)[7]
                        and get_compute(i)[1:7] == want), None)
            if hit is None: reg_compute(*want); hit = counts()[2] - 1; print("  [依赖] 登记算力要素", f)
            used.add(hit)

def ensure_compute_for(c):
    """若没有算力能满足需求c(例如时段已被之前的运行占用)，按 compute_N 中满足规格的条目再登记一个"""
    if match_compute(c): return
    for f in conf_list("compute"):
        cpu, gpu, mem, fr, to, p = int(f[0]), int(f[1]), int(f[2]), BASE_TIME+int(f[3]), BASE_TIME+int(f[4]), int(f[5])
        if cpu >= c[0] and gpu >= c[1] and mem >= c[2] and fr <= c[3] and to >= c[4] and (c[5] == 0 or p <= c[5]):
            print("  [依赖] 所需算力时段已被占用，补登记一个同规格算力", f)
            reg_compute(cpu, gpu, mem, fr, to, p); return
    sys.exit("run.conf 的 compute_N 里没有能满足该需求的算力规格，无法自动补齐")

def check(name, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond: check.failed = True
check.failed = False
def finish():
    print("\n结论:", "不通过" if check.failed else "通过"); sys.exit(1 if check.failed else 0)
