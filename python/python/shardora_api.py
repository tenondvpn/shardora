import requests
import sha3
import uuid
import hashlib
import tempfile
import os
import time
import json

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
http_port = "23001"

Keypair = namedtuple('Keypair', ['skbytes', 'pkbytes', 'account_id'])
Sign = namedtuple('Sign', ['r', 's', 'v'])

STEP_FROM = 0

def transfer(
        str_prikey: str, 
        to: str, 
        amount: int, 
        step=0, 
        gid="", 
        contract_bytes="", 
        input="", 
        key="", 
        val="", 
        prepayment=0, 
        check_gid_valid=True):
    if gid == "":
        gid = _gen_gid()

    keypair = get_keypair(bytes.fromhex(str_prikey))
    param = get_transfer_params(
        gid, to, amount, 90000000000, 1, 
        keypair, 3, contract_bytes, input, 
        prepayment, step, key, val)
    res = _call_tx(param)
    if res.status_code != 200:
        print(f"invalid status {res.status_code}, message: {res.text}")
        return False
    
    if not check_gid_valid:
        return True
    
    print(f"check gid: {gid}")
    return check_transaction_gid_valid(gid)

def get_account_info(address):
    return _post_data("http://{}:{}/query_account".format("127.0.0.1", 23001), {'address': address})


def call_tx(gid, to, amount, gas_limit, sign_r, sign_s, sign_v, pkbytes_str, key, value):
    params = _get_tx_params(sign=sign,
                            pkbytes=pkbytes,
                            gid=gid,
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

def check_accounts_valid(post_data: dict):
    return _post_data("http://{}:{}/accounts_valid".format("127.0.0.1", 23001), post_data)

def check_prepayments_valid(post_data: dict):
    return _post_data("http://{}:{}/prepayment_valid".format("127.0.0.1", 23001), post_data)

def check_gid_valid(post_data: dict):
    return _post_data("http://{}:{}/commit_gid_valid".format("127.0.0.1", 23001), post_data)

def get_transfer_params(
        gid: str, 
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
    if gid == '':
        gid = _gen_gid()
    sign = _sign_message(keypair=keypair,
                        gid=gid,
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
                            gid=gid,
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

def deploy_contract(
        private_key: str, 
        amount: int, 
        sol_file_path: str, 
        constructor_types: list, 
        constructor_params: list,
        prepayment=0,
        check_gid_valid=False):
    ret, stdout, stderr = _run_once(f"chmod 755 ./solc && ./solc --bin {sol_file_path}")
    # print(f"solc --bin {sol_file_path}")
    func_param = ""
    if len(constructor_types) > 0 and len(constructor_types) == len(constructor_params):
        func_param = keccak256_str(encode_hex(encode(constructor_types, constructor_params)))[2:]

    ret_split = (ret.decode('utf-8')).split("Binary:")
    if len(ret_split) != 2:
        return None
    
    bytes_codes = ret_split[1].strip()
    # print(f"bytes_codes: {bytes_codes}, \nstdout: {stdout}, \nstderr: {stderr}, \nfunc_param: {func_param}")
    call_str = bytes_codes + func_param
    contract_address_hash = keccak256_str(call_str)
    contract_address = contract_address_hash[len(contract_address_hash)-40: len(contract_address_hash)]
    res = transfer(
        str_prikey=private_key, 
        to=contract_address, 
        amount=amount, 
        step=6, 
        contract_bytes=call_str, 
        prepayment=prepayment,
        check_gid_valid=check_gid_valid)
    if not res:
        return None
    
    return contract_address

def contract_prepayment(private_key: str, contract_address: str, prepayment: int, check_res: bool, gid: str):
    if not transfer(
            str_prikey=private_key, 
            to=contract_address, 
            amount=0, 
            check_gid_valid=check_res,
            gid=gid,
            step=7, 
            prepayment=prepayment):
        return False
    
    if not check_res:
        return True
    
    keypair = get_keypair(bytes.fromhex(private_key))

    return check_contract_prepayment_success(
        address=keypair.account_id, 
        contract_address=contract_address, 
        prepayment=prepayment)

def check_contract_deploy_success(contract_address: str, try_times=30):
    for i in range(0, 30):
        res = post_data("http://82.156.224.174:801/zjchain/check_contract_deploy_success/", 
                        data={"contract_address": contract_address})
        # print(res)
        # print(res.text)
        try:
            res_json = json.loads(res.text)
            if res_json["status"] == 0:
                return True
        except:
            pass
        
        time.sleep(1)

    return False

def check_contract_prepayment_success(address: str, contract_address: str, prepayment: int, try_times=30):
    for i in range(0, 30):
        res = post_data(
            "http://82.156.224.174:801/zjchain/check_contract_prepayment_success/", 
            data={"contract_address": contract_address, "address": address, "prepayment": prepayment})
        # print(res)
        # print(res.text)
        try:
            res_json = json.loads(res.text)
            if res_json["status"] == 0:
                return True
        except:
            pass
        
        time.sleep(1)

    return False

def call_contract_function(
        private_key: str, 
        contract_address: str, 
        amount: int, 
        function: str, 
        types_list: list, 
        params_list: list):
    func_param = (keccak256_str(f"{function}({','.join(types_list)})")[:8] + 
        encode_hex(encode(types_list, params_list))[2:])

    # print(f"func_param: {func_param}")
    return transfer(str_prikey=private_key, to=contract_address, amount=amount, step=8, input=func_param)

def query_contract_function(
        private_key: str, 
        contract_address: str, 
        function: str, 
        types_list: list, 
        params_list: list):
    func_param = (keccak256_str(f"{function}({','.join(types_list)})")[:8] + 
        encode_hex(encode(types_list, params_list))[2:])

    # print(f"func_param: {func_param}")
    keypair = get_keypair(bytes.fromhex(private_key))
    # print(keypair.account_id)
    res = post_data(f"http://{http_ip}:{http_port}/query_contract", data = {
        "input": func_param,
        'address': contract_address,
        'from': keypair.account_id,
    })

    return res

def check_transaction_gid_valid(in_gid):
    for i in range(0, 30):
        res = check_gid_valid({"gids": [in_gid]})
        if res.status_code != 200:
            print("post check gids failed!")
        else:
            json_res = json.loads(res.text)
            print(json_res)
            print(in_gid)
            if json_res["gids"] is not None:
                for gid in json_res["gids"]:
                    print(f"{in_gid} == {gid} : {(in_gid == gid)}")
                    if in_gid == gid:
                        return True
        time.sleep(1)

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
        gid: str, 
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
    b = decode_hex(gid) + \
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


def _get_tx_params(sign, pkbytes: bytes, gid: str, gas_limit: int, gas_price: int,
                   to: str, amount: int, prepay: int, contract_bytes: str, des_shard_id: int,
                   input: str, step: int, key: str, val: str):
    ret = {
        'gid': gid,
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