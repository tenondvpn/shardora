import requests
import sha3
import uuid
import hashlib
import tempfile
import os
import time
import json

from horae import linux_file_cmd
from eth_keys import keys, datatypes
from secp256k1 import PrivateKey, PublicKey
from eth_utils import decode_hex, encode_hex
from ecdsa import SigningKey, SECP256k1
from web3 import Web3, AsyncWeb3, eth, utils
from eth_keys import keys, datatypes
from eth_utils import decode_hex, encode_hex
from eth_abi import encode
from urllib.parse import urlencode
from eth_account.datastructures import SignedMessage
from eth_account.messages import encode_defunct, encode_structured_data, SignableMessage
from eth_account import Account
from collections import namedtuple
from coincurve import PrivateKey as cPrivateKey

w3 = Web3(Web3.IPCProvider('/Users/myuser/Library/Ethereum/geth.ipc'))

http_ip = "127.0.0.1"
http_port = 23001

Keypair = namedtuple('Keypair', ['skbytes', 'pkbytes', 'account_id'])
Sign = namedtuple('Sign', ['r', 's', 'v'])

STEP_FROM = 0

def transfer(
        str_prikey: str,
        to: str,
        amount: int,
        nonce=-1,
        step=0,
        contract_bytes="",
        input="",
        key="",
        val="",
        prepayment=0,
        check_tx_valid=True,
        gas_limit=999999):
    keypair = get_keypair(bytes.fromhex(str_prikey))
    addr = keypair.account_id
    if step == 8:
        addr = to + keypair.account_id

    if nonce == -1:
        add_info = get_account_info(addr)
        if add_info is None:
            print(f"get address from chain failed: {addr}")
            return False

        print(f"get address: {addr} info: {add_info}")
        nonce = int(add_info["nonce"]) + 1

    param = get_transfer_params(
        nonce, to, amount, gas_limit, 1,
        keypair, 3, contract_bytes, input,
        prepayment, step, key, val)
    json_str = json.dumps(param)
    print(f"tx size: {len(json_str)}")
    res = _call_tx(param)
    if res.status_code != 200:
        print(f"invalid status {res.status_code}, message: {res.text}")
        return False

    if not check_tx_valid:
        return True

    print(f"check nonce: {addr} {nonce}")
    return check_addr_nonce_valid(addr, nonce)

def get_account_info(address):
    res = _post_data("http://{}:{}/query_account".format(http_ip, http_port), {'address': address})
    if res.status_code != 200:
        return None

    json_res = json.loads(res.text)
    return json_res

def call_tx(nonce, to, amount, gas_limit, sign_r, sign_s, sign_v, pkbytes_str, key, value):
    params = _get_tx_params(sign=sign,
                            pkbytes=pkbytes,
                            nonce=nonce,
                            to=to,
                            amount=amount,
                            prepay=0,
                            gas_limit=1000000,
                            gas_price=1,
                            contract_bytes="",
                            input="",
                            des_shard_id=3,
                            step=0,
                            key="confirm",
                            val=value)
    return _call_tx(params)

def post_data(path: str, data: dict):
    return _post_data(path, data)

def gen_gid() -> str:
    return _gen_gid()

def keccak256_str(s: str) -> str:
    return _keccak256_str(s)

def check_address_valid(address, balance=0):
    post_data = {"addrs":[address], "balance": balance}
    res = _post_data("http://{}:{}/accounts_valid".format(http_ip, http_port), post_data)
    if res.status_code != 200:
        return False

    json_res = json.loads(res.text)
    if "addrs" in json_res and json_res["addrs"] is not None:
        for addr in json_res["addrs"]:
            if addr == address:
                return True

    return False


def check_accounts_valid(post_data: dict):
    return _post_data("http://{}:{}/accounts_valid".format(http_ip, http_port), post_data)

def check_prepayments_valid(post_data: dict):
    return _post_data("http://{}:{}/prepayment_valid".format(http_ip, http_port), post_data)

def get_transfer_params(
        nonce: int,
        to: str,
        amount: int,
        gas_limit: int,
        gas_price: int,
        keypair: Keypair,
        des_shard_id: int,
        contract_bytes: str,
        input: str,
        prepay: int,
        step: 0,
        key: str,
        val: str):
    sign = _sign_message(keypair=keypair,
                        nonce=nonce,
                        to=to,
                        amount=amount,
                        gas_limit=gas_limit,
                        gas_price=gas_price,
                        step=step,
                        contract_bytes=contract_bytes,
                        input=input,
                        prepay=prepay,
                        key=key,
                        val=val)
    params = _get_tx_params(sign=sign,
                            pkbytes=keypair.pkbytes,
                            nonce=nonce,
                            to=to,
                            amount=amount,
                            prepay=prepay,
                            gas_limit=gas_limit,
                            gas_price=gas_price,
                            contract_bytes=contract_bytes,
                            input=input,
                            des_shard_id=des_shard_id,
                            step=step,
                            key=key,
                            val=val)
    return params

def get_pk_and_cpk(skbytes: bytes) -> tuple[bytes, bytes, bytes]:
    privkey = skbytes
    sk = SigningKey.from_string(privkey, curve=SECP256k1)
    pk = sk.verifying_key
    public_key = pk.to_string()
    compressed_public_key = pk.to_string("compressed")
    return privkey, public_key, compressed_public_key


def random_skbytes() -> bytes:
    # 生成 32 字节的随机数作为私钥
    sk = SigningKey.generate(curve=SECP256k1)

    return sk.to_string()

def skbytes2account(skbytes: bytes) -> str:
    sk = skbytes.hex()
    _, pk_bytes, _ = get_pk_and_cpk(sk)
    return pkbytes2account(pk_bytes)


def pkbytes2account(pub_key_bytes: bytes) -> str:
    addr = _keccak256_bytes(pub_key_bytes)
    return addr[len(addr)-40:len(addr)]


def get_keypair(skbytes: bytes) -> Keypair:
    _, pkbytes, _ = get_pk_and_cpk(skbytes=skbytes)
    addr = _keccak256_bytes(pkbytes)
    account_id = addr[len(addr)-40:len(addr)]
    return Keypair(skbytes=skbytes, pkbytes=decode_hex('04'+pkbytes.hex()), account_id=account_id)

def deploy_contract_with_bytes(
        private_key: str,
        amount: int,
        bytes_codes: str,
        constructor_types: list,
        constructor_params: list,
        nonce = -1,
        prepayment=0,
        check_tx_valid=False,
        is_library=False,
        contract_address=None):
    func_param = ""
    if len(constructor_types) > 0 and len(constructor_types) == len(constructor_params):
        func_param = encode_hex(encode(constructor_types, constructor_params))[2:]

    if bytes_codes is None:
        print("get sol bytes code failed!")
        return None

    call_str = bytes_codes + func_param
    if contract_address is None:
        contract_address_hash = keccak256_str(call_str+gen_gid())
        contract_address = contract_address_hash[len(contract_address_hash)-40: len(contract_address_hash)]

    step = 6
    if is_library:
        step = 14

    res = transfer(
        str_prikey=private_key,
        to=contract_address,
        amount=amount,
        step=step,
        nonce=nonce,
        contract_bytes=call_str,
        prepayment=prepayment,
        check_tx_valid=check_tx_valid)
    if not res:
        return None

    if check_tx_valid:
        for i in range(0, 30):
            if prepayment > 0:
                keypair = get_keypair(bytes.fromhex(private_key))
                if check_address_valid(contract_address + keypair.account_id, prepayment):
                    return contract_address

            elif check_address_valid(contract_address):
                return contract_address

            time.sleep(3)

    return None

def deploy_contract(
        private_key: str,
        amount: int,
        sol_file_path: str,
        constructor_types: list,
        constructor_params: list,
        nonce = -1,
        prepayment=0,
        check_tx_valid=False,
        is_library=False,
        in_libraries="",
        contract_address=None):
    libraries = ""
    if in_libraries != "":
        libraries = f"--libraries '{in_libraries}'"

    file_name = sol_file_path.split('/')[-1].split('.')[0]
    cmd = f"/usr/bin/solc {libraries} --overwrite --bin {sol_file_path} -o {file_name}"
    ret, stdout, stderr = _run_once(cmd)
    print(cmd)
    # print(f"solc --bin {sol_file_path}")
    func_param = ""
    if len(constructor_types) > 0 and len(constructor_types) == len(constructor_params):
        func_param = encode_hex(encode(constructor_types, constructor_params))[2:]

    bytes_codes = None
    file_cmd = linux_file_cmd.LinuxFileCommand()
    contract_line = None
    with open(sol_file_path, 'r') as f:
        for line in f.readlines():
            if line.find('contract') >= 0:
                contract_line = line
                break

    file_list = file_cmd.list_files(f'./{file_name}/')
    for file in file_list:
        file_name = file.split('/')[-1].split('.')[0]
        if contract_line.find(file_name) >= 0:
            print(f"read contract file: {file} contract_line: {contract_line}")
            with open(file, "r") as f:
                bytes_codes = f.read()
            break

    if bytes_codes is None:
        print("get sol bytes code failed!")
        return None

    print(f"bytes_codes: {bytes_codes}, \nstdout: {stdout}, \nstderr: {stderr}, \nfunc_param: {func_param}", flush=True)
    call_str = bytes_codes + func_param
    if contract_address is None:
        contract_address_hash = keccak256_str(call_str+gen_gid())
        contract_address = contract_address_hash[len(contract_address_hash)-40: len(contract_address_hash)]

    step = 6
    if is_library:
        step = 14

    res = transfer(
        str_prikey=private_key,
        to=contract_address,
        amount=amount,
        step=step,
        nonce=nonce,
        contract_bytes=call_str,
        prepayment=prepayment,
        check_tx_valid=check_tx_valid)
    if not res:
        return None

    if check_tx_valid:
        for i in range(0, 30):
            if prepayment > 0:
                keypair = get_keypair(bytes.fromhex(private_key))
                if check_address_valid(contract_address + keypair.account_id, prepayment):
                    return contract_address

            elif check_address_valid(contract_address):
                return contract_address

            time.sleep(3)

    return None

def contract_prepayment(private_key: str, contract_address: str, prepayment: int, check_res: bool, nonce: int):
    if not transfer(
            str_prikey=private_key,
            to=contract_address,
            amount=0,
            check_tx_valid=check_res,
            nonce=nonce,
            step=7,
            prepayment=prepayment):
        return False

    return True

def call_contract_function(
        private_key: str,
        contract_address: str,
        amount: int,
        function: str,
        types_list: list,
        params_list: list):
    func_param = (keccak256_str(f"{function}({','.join(types_list)})")[:8] +
        encode_hex(encode(types_list, params_list))[2:])

    print(f"func_param: {func_param}")
    return transfer(str_prikey=private_key, to=contract_address, amount=amount, step=8, input=func_param)

def query_contract_function(
        private_key: str,
        contract_address: str,
        function: str,
        types_list: list,
        params_list: list,
        call_type=0):
    func_param = (keccak256_str(f"{function}({','.join(types_list)})")[:8] +
        encode_hex(encode(types_list, params_list))[2:])

    # print(f"func_param: {func_param}")
    keypair = get_keypair(bytes.fromhex(private_key))
    # print(keypair.account_id)
    if call_type == 0:
        res = post_data(f"http://{http_ip}:{http_port}/abi_query_contract", data = {
            "input": func_param,
            'address': contract_address,
            'from': keypair.account_id,
        })
    else:
        res = post_data(f"http://{http_ip}:{http_port}/query_contract", data = {
            "input": func_param,
            'address': contract_address,
            'from': keypair.account_id,
        })

    return res

def check_addr_nonce_valid(addr, in_nonce):
    for i in range(0, 30):
        add_info = get_account_info(addr)
        if add_info is not None and int(add_info['nonce']) >= in_nonce:
            print(f"get address info: {add_info}")
            return True

        time.sleep(3)

    return False

def keccak256_bytes(b: bytes) -> str:
    return _keccak256_bytes(b)

def keccak256_str(s: str) -> str:
    return _keccak256_str(s)


def _keccak256_bytes(b: bytes) -> str:
    k = sha3.keccak_256()
    k.update(b)
    return k.hexdigest()

def _keccak256_str(s: str) -> str:
    k = sha3.keccak_256()
    k.update(bytes(s, 'utf-8'))
    return k.hexdigest()

def _sign_message(
        keypair: Keypair,
        nonce: int,
        to: str,
        amount: int,
        gas_limit: int,
        gas_price: int,
        step: int,
        contract_bytes: str,
        input: str,
        prepay: int,
        key:str,
        val:str):
    frompk = keypair.pkbytes
    b = _long_to_bytes(nonce) + \
         frompk + \
         decode_hex(to) + \
         _long_to_bytes(amount) + \
         _long_to_bytes(gas_limit) + \
         _long_to_bytes(gas_price) + \
         _long_to_bytes(step)
    if contract_bytes != '':
        b += decode_hex(contract_bytes)
    if input != '':
        b += decode_hex(input)
    b += _long_to_bytes(prepay)
    if key != "":
        b += bytes(key, 'utf-8')
        if val != "":
            b += bytes(val, 'utf-8')

    h = _keccak256_bytes(b)
    sign_bytes = cPrivateKey(keypair.skbytes).sign_recoverable(bytes.fromhex(h), hasher=None)

    # message = encode_defunct(hexstr=h)
    # sign = Account.sign_message(message, keypair.skbytes)
    return _parse_sign_bytes(sign_bytes)

def _parse_sign_bytes(sign_bytes) -> Sign:
    if len(sign_bytes) == 65:
    # 分解签名为 r, s, 和 v
        r = sign_bytes[:32]
        s = sign_bytes[32:64]
        v = sign_bytes[64]  # 注意: 在某些情况下，你可能需要对v值进行调整

    r_hex = r.hex()
    s_hex = s.hex()

    return Sign(r=r_hex, s=s_hex, v=v)


def _long_to_bytes(i: int) -> bytes:
    return i.to_bytes(8, 'little')


def _get_random_hex_str() -> str:
    return uuid.uuid4().hex


def _gen_gid() -> str:
    hex_str = _get_random_hex_str()
    ret = hashlib.sha256(hex_str.encode('utf-8')).hexdigest()
    return (64 - len(ret)) * '0' + ret


def _get_tx_params(sign, pkbytes: bytes, nonce: int, gas_limit: int, gas_price: int,
                   to: str, amount: int, prepay: int, contract_bytes: str, des_shard_id: int,
                   input: str, step: int, key: str, val: str):
    ret = {
        'nonce': nonce,
        'pubkey': encode_hex(pkbytes)[2:],
        'to': to,
        'type': step,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'shard_id': des_shard_id,
        'key': key,
        'val': val,
        "pepay": prepay,
        'sign_r': sign.r,
        'sign_s': sign.s,
        'sign_v': sign.v,
        }

    if contract_bytes != '':
        ret['bytes_code'] = contract_bytes

    if input != '':
        ret['input'] = input

    return ret


def _call_tx(post_data: dict):
    return _post_data("http://{}:{}/transaction".format(http_ip, http_port), post_data)


def _post_data(path: str, data: dict):
    querystr = urlencode(data)
    # print(path)
    # print(data)
    res = requests.post(path, data=data, headers={
        'Content-Type': 'application/x-www-form-urlencoded',
        'Content-Length': str(len(bytes(querystr, 'utf-8'))),
    })
    # print(res)
    # print(res.text)
    return res

def _run_once(cmd):
    stdout_file = tempfile.NamedTemporaryFile()
    stderr_file = tempfile.NamedTemporaryFile()
    cmd = '%(cmd)s 1>>%(out)s 2>>%(err)s' % {
            'cmd': cmd,
            'out': stdout_file.name,
            'err': stderr_file.name
        }
    return_code = os.system(cmd)
    # the returnCode of os.system() is encoded by the wait(),
    # it is a 16-bit number, the higher byte is the exit code of the cmd
    # and the lower byte is the signal number to kill the process
    stdout = stdout_file.read()
    stderr = stderr_file.read()
    stdout_file.close()
    stderr_file.close()
    return stdout, stderr, return_code
