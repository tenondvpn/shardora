from __future__ import annotations
import secrets
import struct
import requests
import hashlib
import json
import time
import base64
import binascii
import struct
from enum import IntEnum
from typing import Any, Optional, Union, Dict, List

# Core dependencies
import solcx
import eth_abi
from eth_utils import to_checksum_address
from Crypto.Hash import keccak
from ecdsa import SigningKey, SECP256k1
from ecdsa.util import sigencode_string_canonize

# GMSSL Support
try:
    from gmssl import sm2, sm3, func
except ImportError:
    sm2 = None

try:
    import oqs
except ImportError:
    oqs = None

# --- 1. Constants & Enums ---

class StepType(IntEnum):
    """Defines the specific type of transaction/operation step."""
    kNormalFrom = 0                 # Standard transfer (Sender side)
    kNormalTo = 1                   # Cross-shard confirmation (Sender-side statistics)
    kConsensusRootElectShard = 2    # Shard/Root network election
    kConsensusRootTimeBlock = 3     # Time block creation
    kConsensusCreateGenesisAcount = 4 # Genesis account creation
    kConsensusLocalTos = 5          # Cross-shard confirmation (Receiver-side accumulation)
    kCreateContract = 6             # Contract deployment/creation
    kContractGasPrefund = 7         # Set contract call gas prefund
    kContractExcute = 8             # Execute contract call
    kRootCreateAddress = 9          # Root network address creation
    kStatistic = 12                 # Statistical transaction
    kJoinElect = 13                 # Node election participation
    kCreateLibrary = 14             # Create public contract library (Library)
    kCross = 15                     # Cross-shard anti-loss block replenishment
    kRootCross = 16                 # Root network cross-shard replenishment
    kPoolStatisticTag = 17          # End tag for transaction pool statistics round
    kContractRefund = 18            # contract call gas refund

class MessageHandleStatus(IntEnum):
    """Status codes for message handling and EVM execution."""
    kConsensusSuccess = 0
    kMessageHandle = 10001
    kMessageHandleError = 10002
    kTxAccept = 10003
    kTxInvalidSignature = 10004
    kTxInvalidAddress = 10005
    kTxPoolFullReject = 10006
    kTxUserNonceInvalid = 10007
    kUnkonwn = 10008
    kRequestInvalid = 10009
    kNotExists = 10010

    # --- EVMC Standard Runtime Status ---
    EVMC_SUCCESS = 0                # Execution finished with success
    EVMC_FAILURE = 1                # Generic execution failure
    EVMC_REVERT = 2                 # Execution terminated by REVERT opcode
    EVMC_OUT_OF_GAS = 3             # Execution ran out of gas
    EVMC_INVALID_INSTRUCTION = 4    # Hit an INVALID instruction
    EVMC_UNDEFINED_INSTRUCTION = 5  # Encountered an undefined instruction
    EVMC_STACK_OVERFLOW = 6         # EVM stack limit exceeded
    EVMC_STACK_UNDERFLOW = 7        # Opcode required more items than available
    EVMC_BAD_JUMP_DESTINATION = 8   # Violated jump destination restrictions
    EVMC_INVALID_MEMORY_ACCESS = 9  # Tried to read/write outside memory bounds
    EVMC_CALL_DEPTH_EXCEEDED = 10   # Call depth exceeded the limit
    EVMC_STATIC_MODE_VIOLATION = 11 # Restricted operation attempted in static mode
    EVMC_PRECOMPILE_FAILURE = 12    # Failure in precompiled or system contract
    EVMC_CONTRACT_VALIDATION_FAILURE = 13 # Contract validation failed
    EVMC_ARGUMENT_OUT_OF_RANGE = 14 # Argument value outside of accepted range
    EVMC_WASM_UNREACHABLE_INSTRUCTION = 15 # WASM unreachable instruction hit
    EVMC_WASM_TRAP = 16             # WASM trap hit
    EVMC_INSUFFICIENT_BALANCE = 17  # Caller lacks funds for value transfer

    # --- Internal Errors & Rejections ---
    EVMC_INTERNAL_ERROR = -1        # Generic internal EVM implementation error
    EVMC_REJECTED = -2              # Message/code rejected by the EVM
    EVMC_OUT_OF_MEMORY = -3         # Failed to allocate memory

    kConsensusError = 5001
    kConsensusAdded = 5002,
    kConsensusNotExists = 5004
    kConsensusTxAdded = 5005
    kConsensusNoNewTxs = 5006
    kConsensusInvalidPackage = 5007
    kConsensusTxNotExists = 5008
    kConsensusAccountNotExists = 5009
    kConsensusAccountBalanceError = 5010
    kConsensusAccountExists = 5011
    kConsensusBlockHashError = 5012
    kConsensusBlockHeightError = 5013
    kConsensusPoolIndexError = 5014
    kConsensusBlockNotExists = 5015
    kConsensusBlockPreHashError = 5016
    kConsensusNetwokInvalid = 5017
    kConsensusLeaderInfoInvalid = 5018
    kConsensusExecuteContractFailed = 5019
    kConsensusGasUsedNotEqualToLeaderError = 5020
    kConsensusUserSetGasLimitError = 5021
    kConsensusCreateContractKeyError = 5022
    kConsensusContractAddressLocked = 5023
    kConsensusContractBytesCodeError = 5024
    kConsensusTimeBlockHeightError = 5025
    kConsensusElectBlockHeightError = 5026
    kConsensusLeaderTxInfoInvalid = 5027
    kConsensusVssRandomNotMatch = 5028
    kConsensusWaiting = 5029
    kConsensusOutOfGas = 5030
    kConsensusRevert = 5031
    kConsensusInvalidInstruction = 5032
    kConsensusUndefinedInstruction = 5033
    kConsensusStackOverflow = 5034
    kConsensusStackUnderflow = 5035
    kConsensusBadJumpDestination = 5036
    kConsensusInvalidMemoryAccess = 5037
    kConsensusCallDepthExceeded = 5038
    kConsensusStaticModeViolation = 5039
    kConsensusPrecompileFailure = 5040
    kConsensusContractValidationFailure = 5041
    kConsensusArgumentOutOfRange = 5042
    kConsensusWasmRnreachableInstruction = 5043
    kConsensusWasmTrap = 5044
    kConsensusInsufficientBalance = 5045
    kConsensusInternalError = 5046
    kConsensusRejected = 5047
    kConsensusOutOfMemory = 5048
    kConsensusOutOfPrefund = 5049
    kConsensusElectNodeExists = 5050
    kConsensusNonceInvalid = 5051
    kConsensusJoinElectThreashTInvalid = 5052

# --- 2. Utilities ---
def get_sm2_public_key(private_key_hex: str) -> str:
    """
    针对 gmssl 3.2.2 封装极深的情况。
    通过提取内部私钥对象的公钥点坐标来获取 X+Y。
    Handles the deeply encapsulated gmssl 3.2.2 case.
    Obtains X+Y by extracting the public key point coordinates from the internal private key object.
    """
    from gmssl import sm2
    import binascii

    pk_clean = private_key_hex.replace('0x', '')
    sm2_crypt = sm2.CryptSM2(public_key='', private_key=pk_clean)
    
    # 2. Attempt to directly get point coordinates from the internal private key object
    # In gmssl 3.x, the private key is usually stored in a private variable and is an object with point information
    try:
        internal_key = None
        if hasattr(sm2_crypt, 'private_key'):
            internal_key = sm2_crypt.private_key
            
        # If public key export is implemented internally, call it directly
        # Note: This is a guessed hidden export path based on gmssl 3.2.2 source code logic
        d = int(pk_clean, 16)
        
        # Approach A: Leverage the calculation process that must exist in its signing logic
        P = 0xFFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFF
        A = 0xFFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFC
        B = 0x28E9FA9E9D9F5E344D5A9E4BCF6509A7F39789F515AB8F92DDBCBD414D940E93
        GX = 0x32C4AE2C1F1981195F9904466A39C9948FE30BBFF2660BE1715A4589334C74C7
        GY = 0xBC3736A2F4F6779C59BDCEE36B692153D0A9877CC62A474002DF32E52139F0A0
        
        def ecc_add(P1, P2):
            if P1 is None: return P2
            if P2 is None: return P1
            x1, y1 = P1; x2, y2 = P2
            if x1 == x2 and y1 != y2: return None
            if x1 == x2:
                m = (3 * x1 * x1 + A) * pow(2 * y1, P - 2, P) % P
            else:
                m = (y2 - y1) * pow(x2 - x1, P - 2, P) % P
            x3 = (m * m - x1 - x2) % P
            y3 = (m * (x1 - x3) - y1) % P
            return x3, y3

        def ecc_mul(k, G):
            res = None
            tmp = G
            while k:
                if k & 1: res = ecc_add(res, tmp)
                tmp = ecc_add(tmp, tmp)
                k >>= 1
            return res

        # Execute P = dG calculation
        pub_point = ecc_mul(d, (GX, GY))
        x_hex = hex(pub_point[0])[2:].zfill(64)
        y_hex = hex(pub_point[1])[2:].zfill(64)
        
        return x_hex + y_hex

    except Exception as e:
        raise RuntimeError(f"SM2 derivation critical failure: {str(e)}")

def calc_create2_address(sender: str, salt: str, bytecode: str) -> str:
    sender = sender.lower().replace('0x', '')
    bytecode = bytecode.lower().replace('0x', '')
    
    # Ensure salt is hex; if it's a plain string like 'l1', encode it to hex
    salt_clean = str(salt).lower().replace('0x', '')
    try:
        salt_bytes = bytes.fromhex(salt_clean).zfill(32)
    except ValueError:
        # Fallback: if not hex, hash the string to get a valid 32-byte hex salt
        salt_bytes = keccak.new(digest_bits=256).update(str(salt).encode()).digest()
    
    code_hash = keccak.new(digest_bits=256).update(bytes.fromhex(bytecode)).digest()
    input_data = bytes.fromhex("ff") + bytes.fromhex(sender) + salt_bytes + code_hash
    return keccak.new(digest_bits=256).update(input_data).digest()[-20:].hex().lower()

def compile_and_link(source: str, name: str, libs: Dict[str, str] = None):
    """Compiles Solidity and replaces Library linking placeholders."""
    try:
        solcx.install_solc("0.8.30")
        solcx.set_solc_version("0.8.30")
    except: pass

    compiled = solcx.compile_source(source, output_values=['bin', 'abi'], optimize=True, evm_version='shanghai')
    
    # Flexible lookup to handle solc naming
    contract_data = None
    for key in compiled.keys():
        if key.endswith(f":{name}"):
            contract_data = compiled[key]
            break
            
    if not contract_data:
        raise KeyError(f"Contract '{name}' not found. Available: {list(compiled.keys())}")

    bytecode = contract_data['bin']
    if libs:
        for lib, addr in libs.items():
            placeholder = keccak.new(digest_bits=256).update(f"<stdin>:{lib}".encode()).hexdigest()[:34]
            bytecode = bytecode.replace(f"__${placeholder}$__", addr.lower().replace('0x', ''))
            
    return bytecode, contract_data['abi']

# --- 3. Web3 Mock Components ---

class ShardoraMethod:
    def __init__(self, contract: ShardoraContract, abi_item: dict):
        self.contract = contract
        self.name = abi_item['name']
        self.input_types = [p['type'] for p in abi_item.get('inputs', [])]
        self.output_types = [p['type'] for p in abi_item.get('outputs', [])]

    def __call__(self, *args) -> ShardoraMethod:
        sig = f"{self.name}({','.join(self.input_types)})"
        selector = keccak.new(digest_bits=256).update(sig.encode()).digest()[:4].hex()
        self.encoded_input = selector + eth_abi.encode(self.input_types, args).hex()
        return self

    def call(self) -> Any:
        raw_res = self.contract.client.query_contract(
            self.contract.sender_address, self.contract.address, self.encoded_input
        )
        
        # Fallback values that won't crash .hex() or index lookups
        # Matches (uint256, uint256, bytes32, bytes32, bool)
        default_return = [0, 0, b'\x00'*32, b'\x00'*32, False]

        if not raw_res or "error" in str(raw_res).lower():
            return default_return

        try:
            if isinstance(raw_res, bytes):
                clean_bytes = raw_res
            else:
                clean_str = str(raw_res).replace('0x', '').strip()
                clean_bytes = bytes.fromhex(clean_str)

            return eth_abi.decode(self.output_types, clean_bytes)
            
        except Exception as e:
            print(f"DEBUG: Decoding failed. Raw length: {len(clean_bytes) if 'clean_bytes' in locals() else 0} bytes")
            # If it's a single return value, return 0; if it's a tuple, return our safe defaults
            return default_return if len(self.output_types) > 1 else 0

    def transact(self, private_key: str, value: int = 0, prefund: int = 10**6, oqs_pubkey: str = None, gm_mode: bool = False) -> dict:
        """
        Transaction logic with automatic parsing. 
        Supports GMSSL (via gm_mode), OQS (auto-detection), and standard ECDSA.
        """
        
        # 1. Prioritize GmSSL mode determination
        if gm_mode:
            gm_pubkey = get_sm2_public_key(private_key)
            tx_hash = self.contract.client.send_gmssl_transaction( # Automatically derive 64-byte public key (X+Y)
                private_key, 
                gm_pubkey, 
                self.contract.address, 
                StepType.kContractExcute, 
                amount=value, 
                input_hex=self.encoded_input, 
                prefund=prefund
            )
            
        # 2. Determine Post-Quantum mode (OQS) - based on private key length
        elif len(private_key) > 128:
            if not oqs_pubkey:
                raise ValueError(
                    "OQS detected by key length, but 'oqs_pubkey' is not set. "
                    "Please provide it in the method call."
                )

            tx_hash = self.contract.client.send_oqs_transaction(
                private_key, 
                oqs_pubkey, 
                self.contract.address, 
                StepType.kContractExcute, 
                amount=value, 
                input_hex=self.encoded_input, 
                prefund=prefund
            )
        # 3. Execute standard ECDSA logic
        else:
            tx_hash = self.contract.client.send_transaction_auto(
                private_key, 
                self.contract.address, 
                StepType.kContractExcute, 
                amount=value, 
                input_hex=self.encoded_input, 
                prefund=prefund
            )

        # 4. Wait for and return the receipt
        return self.contract.client.wait_for_receipt(
            tx_hash, 
            abi=self.contract.abi, 
            function_name=self.name
        )
    
class ShardoraContract:
    def __init__(self, client: ShardoraClient, address: Optional[str], abi: list, bytecode: str = None, sender_address: str = ""):
        self.client, self.address, self.abi, self.bytecode, self.sender_address = client, address, abi, bytecode, sender_address
        self.functions = type('Functions', (), {})()
        self.deploy_receipt = None
        if abi:
            for item in [i for i in abi if i.get('type') == 'function']:
                setattr(self.functions, item['name'], self._create_method(item))

    def _create_method(self, item):
        return lambda *args: ShardoraMethod(self, item)(*args)
    
    def transact(self, private_key: str, value: int = 0, prefund: int = 10**6, oqs_pubkey: str = None, gm_mode: bool = False) -> dict:
        """
        核心交易触发逻辑：支持 ECDSA, OQS 和 GmSSL。
        Core transaction triggering logic: supports ECDSA, OQS, and GmSSL.
        """
        # 1. Automatic routing logic
        # Priority: GmSSL > OQS > ECDSA
        if gm_mode:
            # Automatically derive 64-byte public key
            gm_pubkey = get_sm2_public_key(private_key)
            tx_hash = self.contract.client.send_gmssl_transaction(
                private_key, 
                gm_pubkey, 
                self.contract.address, 
                StepType.kContractExcute, 
                amount=value, 
                input_hex=self.encoded_input, 
                prefund=prefund
            )
        elif len(private_key) > 128: # OQS logic
            if not oqs_pubkey:
                raise ValueError("OQS requires 'oqs_pubkey' or setting it in contract object.")
            tx_hash = self.contract.client.send_oqs_transaction(
                private_key, 
                oqs_pubkey, 
                self.contract.address, 
                StepType.kContractExcute, 
                amount=value, 
                input_hex=self.encoded_input, 
                prefund=prefund
            )
        else: # Standard ECDSA
            tx_hash = self.contract.client.send_transaction_auto(
                private_key, 
                self.contract.address, 
                StepType.kContractExcute, 
                amount=value, 
                input_hex=self.encoded_input, 
                prefund=prefund
            )

        # 2. Wait for receipt
        return self.contract.client.wait_for_receipt(
            tx_hash, 
            abi=self.contract.abi, 
            function_name=self.name
        )

    def prefund(self, amount: int, private_key: str, oqs_pubkey: Optional[str] = None, gm_mode: bool = False) -> dict:
        """
        Exposes prefund as a method of the contract object.
        Supports standard, OQS, and GMSSL modes.
        """
        if not self.address:
            raise ValueError("Contract address is not set. Deploy or bind first.")

        if gm_mode:
            gm_pubkey = get_sm2_public_key(private_key)
            tx_hash = self.client.send_gmssl_transaction(
                private_key, gm_pubkey, self.address, StepType.kContractGasPrefund, prefund=amount
            )
        elif len(private_key) > 128:
            if not oqs_pubkey:
                raise ValueError("OQS detected, but 'oqs_pubkey' is not set.")
            tx_hash = self.client.send_oqs_transaction(
                private_key, oqs_pubkey, self.address, StepType.kContractGasPrefund, prefund=amount
            )
        else:
            tx_hash = self.client.send_transaction_auto(
                private_key, self.address, StepType.kContractGasPrefund, prefund=amount
            )
            
        return self.client.wait_for_receipt(tx_hash)
    
    def refund(self, private_key: str, oqs_pubkey: Optional[str] = None, gm_mode: bool = False) -> dict:
        if not self.address:
            raise ValueError("Contract address is not set.")

        if gm_mode:
            gm_pubkey = get_sm2_public_key(private_key)
            tx_hash = self.client.send_gmssl_transaction(
                private_key, gm_pubkey, self.address, StepType.kContractRefund
            )
        elif len(private_key) > 128:
            if not oqs_pubkey:
                raise ValueError("OQS detected, but 'oqs_pubkey' is not set.")
            tx_hash = self.client.send_oqs_transaction(
                private_key, oqs_pubkey, self.address, StepType.kContractRefund
            )
        else:
            tx_hash = self.client.send_transaction_auto(
                private_key, self.address, StepType.kContractRefund
            )
            
        return self.client.wait_for_receipt(tx_hash)
    
    def get_prefund(self, user_address: str) -> int:
        """
        Queries the prefund balance for a specific user on this contract.
        Prefund ID = ContractAddress + UserAddress
        """
        if not self.address:
            raise ValueError("Contract address not set.")
        
        # Calculate the unique prefund ID
        prefund_id = self.address.lower().replace('0x','') + user_address.lower().replace('0x','')
        
        # Query the account info for this specific ID
        return self.client.get_prefund(prefund_id)
    
    def deploy(self, transaction: dict, private_key: str) -> ShardoraContract:
        """
        Web3-style deployment. 
        Automatically detects OQS based on private_key length, 
        or triggers GMSSL mode if 'gm_pubkey' is provided or 'gm_mode' is True.
        """
        # 1. Extract base parameters
        sender = transaction.get('from', self.sender_address)
        salt = str(transaction.get('salt', '0'))
        step = transaction.get('step', StepType.kCreateContract)
        amount = transaction.get('amount', 0)
        args = transaction.get('args', [])
        
        # Get GmSSL mode identifier
        gm_mode = transaction.get('gm_mode', False)
        gm_pubkey = transaction.get('gm_pubkey')

        # 2. Process bytecode and constructor arguments
        full_bytecode = self.bytecode
        if args:
            ctor = next((x for x in self.abi if x['type'] == 'constructor'), None)
            if ctor:
                full_bytecode += eth_abi.encode([i['type'] for i in ctor['inputs']], args).hex()

        # 3. Calculate deployment address
        self.address = calc_create2_address(sender, salt, full_bytecode)

        # 4. Select transaction interface
        # 4a. Prioritize GmSSL mode determination
        if gm_mode or gm_pubkey:
            if not gm_pubkey:
                # If gm_mode is marked but no public key is provided, it is automatically derived from the private key
                gm_pubkey = get_sm2_public_key(private_key)
            
            print(f"gm_pubkey: {gm_pubkey}")
            tx_hash = self.client.send_gmssl_transaction(
                private_key, 
                gm_pubkey, 
                self.address, 
                step, 
                contract_code=full_bytecode, 
                prefund=10000000, 
                amount=amount
            )
            
        # 4b. Determine Post-Quantum mode (OQS) - based on private key length
        elif len(private_key) > 128:
            oqs_pubkey = transaction.get('pubkey')
            if not oqs_pubkey:
                raise ValueError("OQS deployment requires 'pubkey' inside the transaction dict.")
                
            tx_hash = self.client.send_oqs_transaction(
                private_key, 
                oqs_pubkey, 
                self.address, 
                step, 
                contract_code=full_bytecode, 
                prefund=10000000, 
                amount=amount
            )
            
        # 4c. Standard ECDSA logic
        else:
            tx_hash = self.client.send_transaction_auto(
                private_key, 
                self.address, 
                step, 
                contract_code=full_bytecode, 
                prefund=10000000, 
                amount=amount
            )

        # 5. Wait for and return the result
        self.deploy_receipt = self.client.wait_for_receipt(tx_hash)
        return self

class ShardoraWeb3Mock:
    def __init__(self, host: str, port: int):
        self.client = ShardoraClient(host, port)
        self.shardora = self

    def contract(self, address: str = None, abi: list = None, bytecode: str = None, sender_address: str = ""):
        return ShardoraContract(self.client, address, abi, bytecode, sender_address)
    
    def send_transaction(self, tx_dict: dict, private_key: str) -> dict:
        tx_hash = self.client.send_transaction_auto(private_key, tx_dict['to'], StepType.kNormalFrom, amount=tx_dict.get('value', 0))
        return self.client.wait_for_receipt(tx_hash)
    
    def send_oqs_transaction(self, tx_dict: dict, private_key: str) -> dict:
        """Fix: Send Post-Quantum (OQS) transaction"""
        # Must get the OQS public key from tx_dict, as OQS signature requires it
        pubkey = tx_dict.get('pubkey')
        if not pubkey:
            raise ValueError("OQS transaction requires 'pubkey' in tx_dict")
            
        # Call the method specifically handling OQS in the client
        tx_hash = self.client.send_oqs_transaction(
            private_key,     # The private_key here should be the OQS private key
            pubkey,          # OQS public key
            tx_dict['to'], 
            StepType.kNormalFrom, 
            amount=tx_dict.get('value', 0)
        )
        return self.client.wait_for_receipt(tx_hash)

    def send_gmssl_transaction(self, tx_dict: dict, private_key: str) -> dict:
        # If tx_dict does not provide a public key, it is automatically generated
        pubkey = tx_dict.get('gm_pubkey')
        if not pubkey:
            pubkey = get_sm2_public_key(private_key)
            
        tx_hash = self.client.send_gmssl_transaction(
            private_key, pubkey, tx_dict['to'], StepType.kNormalFrom,
            amount=tx_dict.get('value', 0)
        )
        return self.client.wait_for_receipt(tx_hash)
    
# --- 4. Base Client ---

class ShardoraClient:
    def __init__(self, host, port):
        self.base_url = f"http://{host}:{port}"
        self.tx_url = f"{self.base_url}/transaction"
        self.query_url = f"{self.base_url}/query_account"
        self.receipt_url = f"{self.base_url}/transaction_receipt"
        self.query_contract_url = f"{self.base_url}/query_contract"
        self.oqs_url = f"http://{host}:{port}/oqs_transaction"
        self.gmssl_url = f"{self.base_url}/gm_transaction"

    def get_address(self, pk_hex):
        sk = SigningKey.from_string(bytes.fromhex(pk_hex.replace('0x', '')), curve=SECP256k1)
        pub = sk.verifying_key.to_string("uncompressed")[1:]
        return keccak.new(digest_bits=256).update(pub).digest()[-20:].hex()

    def send_transaction_auto(self, pk_hex, to, step, amount=0, contract_code='', input_hex='', prefund=0):
        my_addr = self.get_address(pk_hex)
        nonce_addr = to + my_addr if (step == StepType.kContractExcute or step == StepType.kContractRefund) else my_addr
        try:
            r = requests.post(self.query_url, data={"address": nonce_addr}).json()
            nonce = int(r.get("nonce", 0)) + 1
        except: nonce = 1

        sk = SigningKey.from_string(bytes.fromhex(pk_hex.replace('0x', '')), curve=SECP256k1)
        pub = sk.verifying_key.to_string("uncompressed").hex()

        msg = bytearray()
        msg.extend(struct.pack('<Q', nonce))
        msg.extend(bytes.fromhex(pub))
        msg.extend(bytes.fromhex(to.replace('0x','')))
        msg.extend(struct.pack('<Q', amount))
        msg.extend(struct.pack('<Q', 5000000))
        msg.extend(struct.pack('<Q', 1))
        msg.extend(struct.pack('<Q', int(step)))
        if contract_code: msg.extend(bytes.fromhex(contract_code))
        if input_hex: msg.extend(bytes.fromhex(input_hex))
        if prefund > 0: msg.extend(struct.pack('<Q', prefund))

        txh = keccak.new(digest_bits=256).update(msg).digest()
        sig = sk.sign_digest_deterministic(txh, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
        
        data = {"nonce": str(nonce), "pubkey": pub, "to": to, "amount": str(amount), "gas_limit": "5000000", "gas_price": "1", "shard_id": "0", "type": str(int(step)), "sign_r": sig[:32].hex(), "sign_s": sig[32:64].hex(), "sign_v": "0"}
        if contract_code: data["bytes_code"] = contract_code
        if input_hex: data["input"] = input_hex
        if prefund: data["prefund"] = str(prefund)
        
        requests.post(self.tx_url, data=data)
        return txh.hex()

    def wait_for_receipt(self, tx_hash: str, abi: list = None, function_name: str = None) -> dict:
        """Polls for the transaction receipt and automatically calls decode_receipt once retrieved."""
        while True:
            try:
                resp = requests.post(self.receipt_url, data={"tx_hash": tx_hash}).json()
                print(resp)
                # Status codes 10001 (Pending) or 10003 (Accepted) indicate processing is still in progress
                if resp.get("status") not in [10001, 10003]:
                    # Receipt found! Decode if ABI and function name are provided
                    if abi and function_name:
                        return self.decode_receipt(resp, abi, function_name)
                    return resp
            except Exception as ex:
                print(f"Receipt poll error: {ex}")
                
            time.sleep(5)  # Poll once every 5 seconds

    def decode_receipt(self, receipt: dict, abi: list, function_name: str = None) -> dict:
        status = receipt.get("status")
        raw_out_b64 = receipt.get("output", "")
        raw_events = receipt.get("events", [])
        
        receipt['decoded_output'] = None
        receipt['decoded_events'] = []

        # --- 1. Parse Return Value (Output) ---
        if status == 0 and raw_out_b64 and function_name and abi:
            try:
                raw_bytes = base64.b64decode(raw_out_b64)
                item = next((i for i in abi if i.get('name') == function_name), None)
                if item and 'outputs' in item:
                    decoded = eth_abi.decode([o['type'] for o in item['outputs']], raw_bytes)
                    receipt['decoded_output'] = decoded[0] if len(decoded) == 1 else decoded
            except: 
                pass

        # --- 2. Parse Events ---
        if abi and raw_events:
            # First, build a mapping table for topic0 -> event_abi
            event_map = {}
            for item in [i for i in abi if i.get('type') == 'event']:
                sig = f"{item['name']}({','.join([i['type'] for i in item['inputs']])})"
                # Calculate topic0: keccak256("EventName(type1,type2)")
                topic0 = keccak.new(digest_bits=256).update(sig.encode()).digest().hex()
                event_map[topic0] = item

            for e in raw_events:
                try:
                    # Shardora's topics are a list of Base64 encoded strings
                    t0_hex = base64.b64decode(e['topics'][0]).hex()
                    
                    if t0_hex in event_map:
                        spec = event_map[t0_hex]
                        data_bytes = base64.b64decode(e['data'])
                        
                        # Distinguish between indexed and non-indexed parameters
                        # Note: In Shardora's simplified implementation, all data might be packed in 'data',
                        # or partially in 'topics'. This assumes standard EVM logic: non-indexed are in 'data'.
                        types = [i['type'] for i in spec['inputs'] if not i.get('indexed')]
                        names = [i['name'] for i in spec['inputs'] if not i.get('indexed')]
                        
                        vals = eth_abi.decode(types, data_bytes)
                        receipt['decoded_events'].append({
                            "event": spec['name'],
                            "args": dict(zip(names, vals))
                        })
                except Exception as ex:
                    print(f"Event decode error: {ex}")

        return receipt
    
    def get_oqs_address(self, pubkey_hex: str) -> str:
        """
        Fix: Synchronize server-side C++ logic
        str_addr_ = common::Hash::keccak256(str_pk_).substr(0, 20)
        """
        pub_bytes = bytes.fromhex(pubkey_hex.replace('0x', ''))
        # Must use Keccak256
        k = keccak.new(digest_bits=256)
        k.update(pub_bytes)
        return k.digest()[:20].hex()
    
    def send_oqs_transaction(self, oqs_sk_hex, oqs_pk_hex, to, step, amount=0, contract_code='', input_hex='', prefund=0):
        """Send post-quantum transaction - perfectly adapted for 0.15.0/0.14.0 mixed environments"""
        if not oqs:
            raise ImportError("liboqs-python is required")
            
        # sigalg = "ML-DSA-44"
        # oqs_signer = oqs.Signature(sigalg)
        # oqs_verifier = oqs.Signature(sigalg)
        # # signer_public_key = oqs_signer.generate_keypair()
        # oqs_pk_hex = signer_public_key.hex()
        # oqs_sk_hex = oqs_signer.export_secret_key().hex()
        my_addr = self.get_oqs_address(oqs_pk_hex)
        nonce_addr = to + my_addr if (step == StepType.kContractExcute or step == StepType.kContractRefund) else my_addr
        
        # 1. Get Nonce
        try:
            r = requests.post(self.query_url, data={"address": nonce_addr}).json()
            nonce = int(r.get("nonce", 0)) + 1
        except: nonce = 1

        # 2. Construct message hash (Keccak256)
        msg = bytearray()
        msg.extend(struct.pack('<Q', nonce))

        pk_bytes = bytes.fromhex(oqs_pk_hex.replace('0x',''))
        msg.extend(pk_bytes)
        msg.extend(bytes.fromhex(to.replace('0x','')))
        msg.extend(struct.pack('<Q', amount))
        msg.extend(struct.pack('<Q', 5000000)) # gas_limit
        msg.extend(struct.pack('<Q', 1))       # gas_price
        msg.extend(struct.pack('<Q', int(step)))
        if contract_code: msg.extend(bytes.fromhex(contract_code))
        if input_hex: msg.extend(bytes.fromhex(input_hex))
        if prefund > 0: msg.extend(struct.pack('<Q', prefund))

        txh = keccak.new(digest_bits=256).update(msg).digest()

        # print(f"Signature details:{oqs_signer.details}")
        # message = bytes(txh)
        # signature = oqs_signer.sign(message)

        # # Verifier verifies the signature
        # test_is_valid = oqs_verifier.verify(message, signature, bytes.fromhex(oqs_pk_hex.replace('0x','')))

        # print(f"Valid signature {test_is_valid}")

        # 3. Execute ML-DSA-44 (Dilithium2) signature
        with oqs.Signature('ML-DSA-44') as signer:
            import ctypes
            
            # A. Message hash: use native bytes directly (let oqs.py handle create_string_buffer internally)
            txh_bytes = bytes(txh)
            
            # B. Secret key preparation (2560 bytes)
            sk_bytes = bytes.fromhex(oqs_sk_hex.replace('0x', ''))
            sk_len = len(sk_bytes) 
            # sk_bytes = sk_bytes.ljust(sk_len, b'\x00')[:sk_len]
            
            # C. Inject secret key and prevent free() crashes
            # We must put the secret key into a ctypes instance named secret_key,
            # because free() in __exit__ will call byref() on this attribute
            sk_ctypes = (ctypes.c_uint8 * sk_len).from_buffer_copy(sk_bytes)
            
            try:
                # Attempt to overwrite. This might fail if secret_key is a property
                signer.secret_key = sk_ctypes 
                print("0 set prikey success.")
            except:
                # If the above fails, it means it's read-only. We either modify the internal variable or force inject it
                # In 0.14.0, the internal buffer is usually located here
                if hasattr(signer, '_secret_key'):
                    ctypes.memmove(signer._secret_key, sk_ctypes, sk_len)
                    print("1 set prikey success.")
                else:
                    # The last resort: directly replace the object attribute
                    signer.__dict__['secret_key'] = sk_ctypes
                    print("2 set prikey success.")

            # D. Execute signature
            # Passing 1 argument satisfies "2 positional arguments" (self + msg)
            # And avoid manually constructing ctypes to prevent internal create_string_buffer errors
            signature = signer.sign(txh_bytes)

            is_valid = signer.verify(txh, signature, pk_bytes)
            print(f"Local Verify Test: {is_valid}, len pk: {len(pk_bytes)}, sk_len: {sk_len}")

        # 4. Convert back to Hex
        sig_hex = bytes(signature).hex()


        # 4. Assemble request
        data = {
            "nonce": str(nonce),
            "pubkey": oqs_pk_hex.replace('0x',''),
            "to": to.replace('0x',''),
            "amount": str(amount),
            "gas_limit": "5000000",
            "gas_price": "1",
            "shard_id": "0",
            "type": str(int(step)),
            "sign": sig_hex 
        }
        
        if contract_code: data["bytes_code"] = contract_code
        if input_hex: data["input"] = input_hex
        if prefund: data["prefund"] = str(prefund)
        
        requests.post(self.oqs_url, data=data)
        print(f"tx hash {txh.hex()}, pk: {oqs_pk_hex}, data: {data}, msg: {msg.hex()}")
            
        return txh.hex()

    def get_prefund(self, prefund_id: str) -> int:
        """Queries the prefund field from the account status."""
        try:
            # The server expects the composite ID (Contract+User) as the address
            response = requests.post(self.query_url, data={"address": prefund_id}, timeout=5).json()
            # In your C++ backend, this is usually stored in the 'prefund' field of the account
            return int(response.get("balance", 0))
        except Exception as e:
            print(f"DEBUG: Prefund query failed for ID {prefund_id}: {e}")
            return 0
        
    def query_contract(self, f, a, i):
        return requests.post(self.query_contract_url, data={"from": f, "address": a, "input": i}).text
    
    def get_balance(self, a):
        try:
            response = requests.post(self.query_url, data={"address": a}, timeout=5)
            # Check if the response is actually JSON
            return int(response.json().get("balance", 0))
        except Exception as e:
            print(f"DEBUG: Balance query failed for {a}. Response text: '{response.text}'")
            return 0

    def get_nonce(self, a):
        try:
            response = requests.post(self.query_url, data={"address": a}, timeout=5)
            return int(response.json().get("nonce", 0))
        except Exception as e:
            print(f"DEBUG: Nonce query failed for {a}. Response text: '{response.text}'")
            return 0

    def get_gmssl_address(self, pubkey_hex: str) -> str:
        """
        匹配 C++: str_addr_ = common::Hash::sm3(str_pk_).substr(0, 20)
        """
        import binascii
        pub_bytes = binascii.unhexlify(pubkey_hex.replace('0x', ''))
        hash_hex = sm3.sm3_hash(list(pub_bytes))
        return hash_hex[:40]
    
    def send_gmssl_transaction(self, pri_key_hex, pub_key_hex, to, step, amount=0, contract_code='', input_hex='', prefund=0):
        """
        发送国密交易：构造消息 -> SM3摘要 -> SM2签名 -> 发送
        """
        my_addr = self.get_gmssl_address(pub_key_hex)
        nonce_addr = to + my_addr if (step in [StepType.kContractExcute, StepType.kContractRefund]) else my_addr
        
        try:
            r = requests.post(self.query_url, data={"address": nonce_addr}).json()
            nonce = int(r.get("nonce", 0)) + 1
        except: nonce = 1

        msg = bytearray()
        msg.extend(struct.pack('<Q', nonce))

        msg.extend(bytes.fromhex(pub_key_hex.replace('0x','')))
        msg.extend(bytes.fromhex(to.replace('0x','')))
        msg.extend(struct.pack('<Q', amount))
        msg.extend(struct.pack('<Q', 5000000)) # gas_limit
        msg.extend(struct.pack('<Q', 1))       # gas_price
        msg.extend(struct.pack('<Q', int(step)))
        if contract_code: msg.extend(bytes.fromhex(contract_code))
        if input_hex: msg.extend(bytes.fromhex(input_hex))
        if prefund > 0: msg.extend(struct.pack('<Q', prefund))

        # msg = bytearray()
        # msg.extend(struct.pack('<Q', nonce))
        # msg.extend(binascii.unhexlify(pub_key_hex))
        # msg.extend(binascii.unhexlify(to.replace('0x','')))
        # msg.extend(struct.pack('<Q', amount))
        # msg.extend(struct.pack('<Q', 5000000)) # gas_limit
        # msg.extend(struct.pack('<Q', 1))       # gas_price
        # msg.extend(struct.pack('<Q', int(step)))
        # if contract_code: msg.extend(binascii.unhexlify(contract_code))
        # if input_hex: msg.extend(binascii.unhexlify(input_hex))
        # if prefund > 0: msg.extend(struct.pack('<Q', prefund))

        # 3. Calculate SM3 digest as signing object
        txh_hex = sm3.sm3_hash(list(msg))
        txh_bytes = binascii.unhexlify(txh_hex)
        # 4. SM2 Signature (R + S)
        sm2_crypt = sm2.CryptSM2(public_key=pub_key_hex, private_key=pri_key_hex)
        # Get signature result, gmssl library directly returns R+S concatenated hex string
        sig_hex = sm2_crypt.sign(txh_bytes, secrets.token_hex(32))

        data = {
            "nonce": str(nonce),
            "pubkey": pub_key_hex.replace('0x',''),
            "to": to.replace('0x',''),
            "amount": str(amount),
            "gas_limit": "5000000",
            "gas_price": "1",
            "shard_id": "0",
            "type": str(int(step)),
            "sign": sig_hex  # Backend C++ directly memcpy(sig.r, sign.c_str(), 32)
        }
        if contract_code: data["bytes_code"] = contract_code
        if input_hex: data["input"] = input_hex
        if prefund: data["prefund"] = str(prefund)

        requests.post(self.gmssl_url, data=data)
        return txh_hex

