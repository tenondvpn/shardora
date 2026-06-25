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
    kConsensusContractDestructed = 5053

# --- 2. Utilities ---
def get_sm2_public_key(private_key_hex: str) -> str:
    """
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

def calc_create_address(sender: str, nonce: int) -> str:
    """
    Ethereum CREATE address formula: keccak256(RLP([sender, nonce]))[-20:]
    Matches Shardora's GetContractAddress(sender, nonce_bytes).
    """
    sender_bytes = bytes.fromhex(sender.lower().replace('0x', ''))
    # Normalise to 20 bytes
    if len(sender_bytes) < 20:
        sender_bytes = b'\x00' * (20 - len(sender_bytes)) + sender_bytes
    else:
        sender_bytes = sender_bytes[-20:]

    def rlp_bytes(b: bytes) -> bytes:
        if len(b) == 0:
            return b'\x80'
        if len(b) == 1 and b[0] < 0x80:
            return b
        if len(b) <= 55:
            return bytes([0x80 + len(b)]) + b
        len_be = len(b).to_bytes((len(b).bit_length() + 7) // 8, 'big')
        return bytes([0xb7 + len(len_be)]) + len_be + b

    def rlp_list(payload: bytes) -> bytes:
        if len(payload) <= 55:
            return bytes([0xc0 + len(payload)]) + payload
        len_be = len(payload).to_bytes((len(payload).bit_length() + 7) // 8, 'big')
        return bytes([0xf7 + len(len_be)]) + len_be + payload

    # nonce as minimal big-endian bytes (empty for 0)
    if nonce == 0:
        nonce_bytes = b''
    else:
        nonce_bytes = nonce.to_bytes((nonce.bit_length() + 7) // 8, 'big')

    payload = rlp_bytes(sender_bytes) + rlp_bytes(nonce_bytes)
    rlp = rlp_list(payload)
    return keccak.new(digest_bits=256).update(rlp).digest()[-20:].hex().lower()


def calc_create2_address(sender: str, salt: str, bytecode: str) -> str:
    sender = sender.lower().replace('0x', '')
    bytecode = normalize_hex(bytecode)
    
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

def normalize_hex(value: str) -> str:
    """Return a clean even-length lowercase hex string without 0x."""
    if value is None:
        return ''
    clean = str(value).strip().lower()
    if clean.startswith('0x'):
        clean = clean[2:]
    if len(clean) % 2:
        clean = '0' + clean
    return clean

def compile_and_link(source: str, name: str, libs: Dict[str, str] = None):
    """Compiles Solidity and replaces Library linking placeholders."""
    try:
        solcx.set_solc_version("0.8.34")
    except:
        try:
            import shutil, os
            solcx_dir = os.path.expanduser("~/.solcx")
            os.makedirs(solcx_dir, exist_ok=True)
            dst = os.path.join(solcx_dir, "solc-v0.8.34")
            if not os.path.exists(dst):
                shutil.copy("/usr/local/bin/solc", dst)
                os.chmod(dst, 0o755)
            solcx.set_solc_version("0.8.34")
        except: pass

    try:
        compiled = solcx.compile_source(
            source, output_values=['bin', 'abi'], optimize=True, evm_version='shanghai')
    except solcx.exceptions.UnknownValue:
        compiled = solcx.compile_source(
            source, output_values=['bin', 'abi'], optimize=True, evm_version='paris')
    
    # Flexible lookup to handle solc naming
    contract_data = None
    for key in compiled.keys():
        if key.endswith(f":{name}"):
            contract_data = compiled[key]
            break
            
    if not contract_data:
        raise KeyError(f"Contract '{name}' not found. Available: {list(compiled.keys())}")

    bytecode = normalize_hex(contract_data['bin'])
    if libs:
        for lib, addr in libs.items():
            placeholder = keccak.new(digest_bits=256).update(f"<stdin>:{lib}".encode()).hexdigest()[:34]
            bytecode = bytecode.replace(f"__${placeholder}$__", normalize_hex(addr))
            
    return bytecode, contract_data['abi']

# --- 3. Web3 Mock Components ---

def _print_receipt(label: str, receipt: dict, address: str = ""):
    """Print decoded output and events from a transaction receipt."""
    if not receipt or not isinstance(receipt, dict):
        return
    status = receipt.get('status', '?')
    print(f"  [{label}] addr={address[:16]}{'...' if len(address) > 16 else ''} status={status}")
    # Output
    raw_out = receipt.get('output', '')
    decoded_out = receipt.get('decoded_output')
    if decoded_out is not None:
        print(f"    output (decoded): {decoded_out}")
    elif raw_out:
        print(f"    output (raw): {raw_out[:120]}{'...' if len(str(raw_out)) > 120 else ''}")
    # Events
    decoded_events = receipt.get('decoded_events', [])
    if decoded_events:
        for i, ev in enumerate(decoded_events):
            print(f"    event[{i}]: {ev.get('event', '?')} → {ev.get('args', {})}")
    # Error message
    msg = receipt.get('msg', '')
    if status != 0 and msg:
        print(f"    msg: {msg}")


class ShardoraMethod:
    def __init__(self, contract: ShardoraContract, abi_item: dict):
        self.contract = contract
        self.name = abi_item['name']
        self.abi_item = abi_item
        self.input_types = [self._resolve_type(p) for p in abi_item.get('inputs', [])]
        self.output_types = [self._resolve_type(p) for p in abi_item.get('outputs', [])]

    @staticmethod
    def _resolve_type(param: dict) -> str:
        """Resolve ABI parameter type, expanding tuple types into (type1,type2,...) form.
        Handles nested tuples and tuple arrays like tuple[] / tuple[3]."""
        base = param.get('type', '')
        if not base.startswith('tuple'):
            return base
        # Extract array suffix if present, e.g. "tuple[]" → "[]", "tuple[3]" → "[3]"
        suffix = base[5:]  # everything after "tuple"
        components = param.get('components', [])
        inner = ','.join(ShardoraMethod._resolve_type(c) for c in components)
        return f"({inner}){suffix}"

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
        receipt = self.contract.client.wait_for_receipt(
            tx_hash, 
            abi=self.contract.abi, 
            function_name=self.name
        )
        _print_receipt(f"CALL {self.name}", receipt, self.contract.address)
        return receipt
    
class ShardoraContract:
    def __init__(self, client: ShardoraClient, address: Optional[str], abi: list, bytecode: str = None, sender_address: str = ""):
        self.client = client
        self.address = address
        self.abi = abi
        self.bytecode = normalize_hex(bytecode)
        self.sender_address = sender_address
        self.functions = type('Functions', (), {})()
        self.deploy_receipt = None
        if abi:
            for item in [i for i in abi if i.get('type') == 'function']:
                setattr(self.functions, item['name'], self._create_method(item))

    def _create_method(self, item):
        return lambda *args: ShardoraMethod(self, item)(*args)
    
    def transact(self, private_key: str, value: int = 0, prefund: int = 10**6, oqs_pubkey: str = None, gm_mode: bool = False) -> dict:
        """
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
        _print_receipt("DEPLOY", self.deploy_receipt, self.address)
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
        self.base_url = f"https://{host}:{port}"
        self.tx_url = f"{self.base_url}/transaction"
        self.query_url = f"{self.base_url}/query_account"
        self.receipt_url = f"{self.base_url}/transaction_receipt"
        self.query_contract_url = f"{self.base_url}/abi_query_contract"
        self.oqs_url = f"https://{host}:{port}/oqs_transaction"
        self.gmssl_url = f"{self.base_url}/gm_transaction"
        # Disable SSL verification for self-signed certificates
        self.verify_ssl = False
        # Suppress SSL warnings
        import urllib3
        urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

    def get_address(self, pk_hex):
        sk = SigningKey.from_string(bytes.fromhex(pk_hex.replace('0x', '')), curve=SECP256k1)
        pub = sk.verifying_key.to_string("uncompressed")[1:]
        return keccak.new(digest_bits=256).update(pub).digest()[-20:].hex()

    def send_transaction_auto(self, pk_hex, to, step, amount=0, contract_code='', input_hex='', prefund=0):
        my_addr = self.get_address(pk_hex)
        contract_code = normalize_hex(contract_code)
        input_hex = normalize_hex(input_hex)
        nonce_addr = to + my_addr if (step == StepType.kContractExcute or step == StepType.kContractRefund) else my_addr
        try:
            r = requests.post(self.query_url, data={"address": nonce_addr}, verify=self.verify_ssl).json()
            nonce = int(r.get("nonce", 0)) + 1
        except: nonce = 1

        sk = SigningKey.from_string(bytes.fromhex(pk_hex.replace('0x', '')), curve=SECP256k1)
        pub = sk.verifying_key.to_string("uncompressed").hex()

        def _build_and_send(_nonce):
            msg = bytearray()
            msg.extend(struct.pack('<Q', _nonce))
            msg.extend(bytes.fromhex(pub))
            msg.extend(bytes.fromhex(to.replace('0x','')))
            msg.extend(struct.pack('<Q', amount))
            msg.extend(struct.pack('<Q', 5000000))
            msg.extend(struct.pack('<Q', 1))
            msg.extend(struct.pack('<Q', int(step)))
            if contract_code: msg.extend(bytes.fromhex(contract_code))
            if input_hex: msg.extend(bytes.fromhex(input_hex))
            if prefund > 0: msg.extend(struct.pack('<Q', prefund))
            _txh = keccak.new(digest_bits=256).update(msg).digest()
            _sig = sk.sign_digest_deterministic(_txh, hashfunc=hashlib.sha256, sigencode=sigencode_string_canonize)
            _data = {"nonce": str(_nonce), "pubkey": pub, "to": to, "amount": str(amount), "gas_limit": "5000000", "gas_price": "1", "shard_id": "0", "type": str(int(step)), "sign_r": _sig[:32].hex(), "sign_s": _sig[32:64].hex(), "sign_v": "0"}
            if contract_code: _data["bytes_code"] = contract_code
            if input_hex: _data["input"] = input_hex
            if prefund: _data["prefund"] = str(prefund)
            _resp = requests.post(self.tx_url, data=_data, verify=self.verify_ssl)
            return _txh, _resp

        txh, resp = _build_and_send(nonce)
        body = resp.text[:200]
        print(f"[send_tx] status={resp.status_code} body={body}")

        # On nonce invalid: poll until nonce changes (previous tx confirmed), then resend
        if 'kTxUserNonceInvalid' in body:
            old_nonce = nonce - 1  # the nonce we queried before +1
            for retry in range(30):
                time.sleep(1)
                try:
                    r2 = requests.post(self.query_url, data={"address": nonce_addr}, verify=self.verify_ssl).json()
                    cur = int(r2.get("nonce", 0))
                except:
                    continue
                if cur != old_nonce:
                    nonce = cur + 1
                    txh, resp = _build_and_send(nonce)
                    body = resp.text[:200]
                    print(f"[send_tx retry] nonce={nonce} status={resp.status_code} body={body}")
                    if 'kTxUserNonceInvalid' not in body:
                        break
                    old_nonce = cur

        return txh.hex()

    def wait_for_receipt(self, tx_hash: str, abi: list = None, function_name: str = None,
                         timeout: int = 120, not_exists_retries: int = 10) -> dict:
        """Polls for the transaction receipt. Returns None on timeout or persistent kNotExists."""
        deadline = time.time() + timeout
        not_exists_count = 0
        while time.time() < deadline:
            try:
                resp = requests.post(self.receipt_url, data={"tx_hash": tx_hash}, verify=self.verify_ssl).json()
                print(resp)
                status = resp.get("status")
                # kNotExists: tx not yet in pool — retry a limited number of times
                if status == 100010:
                    not_exists_count += 1
                    if not_exists_count >= not_exists_retries:
                        print(f"[wait_receipt] tx {tx_hash} not found after {not_exists_retries} retries, giving up")
                        return resp
                    time.sleep(1)
                    continue
                # kMessageHandle / kTxAccept: still pending
                if status in [10001, 10003]:
                    not_exists_count = 0  # reset — tx is in pool now
                    time.sleep(1)
                    continue
                # Any other status = final result
                if abi and function_name:
                    return self.decode_receipt(resp, abi, function_name)
                return resp
            except Exception as ex:
                print(f"Receipt poll error: {ex}")
                time.sleep(1)
        print(f"[wait_receipt] timeout after {timeout}s for tx {tx_hash}")
        return None

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
        contract_code = normalize_hex(contract_code)
        input_hex = normalize_hex(input_hex)
            
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
            r = requests.post(self.query_url, data={"address": nonce_addr}, verify=self.verify_ssl).json()
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
        
        requests.post(self.oqs_url, data=data, verify=self.verify_ssl)
        print(f"tx hash {txh.hex()}, pk: {oqs_pk_hex}, data: {data}, msg: {msg.hex()}")
            
        return txh.hex()

    def get_prefund(self, prefund_id: str) -> int:
        """Queries the prefund field from the account status."""
        try:
            # The server expects the composite ID (Contract+User) as the address
            response = requests.post(self.query_url, data={"address": prefund_id}, timeout=5, verify=self.verify_ssl).json()
            # In your C++ backend, this is usually stored in the 'prefund' field of the account
            return int(response.get("balance", 0))
        except Exception as e:
            print(f"DEBUG: Prefund query failed for ID {prefund_id}: {e}")
            return 0
        
    def query_contract(self, f, a, i):
        return requests.post(self.query_contract_url, data={"from": f, "address": a, "input": i}, verify=self.verify_ssl).text
    
    def get_balance(self, a):
        response = None
        try:
            response = requests.post(self.query_url, data={"address": a}, timeout=5, verify=self.verify_ssl)
            # Check if the response is actually JSON
            return int(response.json().get("balance", 0))
        except Exception as e:
            response_text = response.text if response is not None else str(e)
            print(f"DEBUG: Balance query failed for {a}. Response text: '{response_text}'")
            return 0

    def get_nonce(self, a):
        response = None
        try:
            response = requests.post(self.query_url, data={"address": a}, timeout=5, verify=self.verify_ssl)
            return int(response.json().get("nonce", 0))
        except Exception as e:
            response_text = response.text if response is not None else str(e)
            print(f"DEBUG: Nonce query failed for {a}. Response text: '{response_text}'")
            return 0

    def get_gmssl_address(self, pubkey_hex: str) -> str:
        """
        Match C++: str_addr_ = common::Hash::sm3(str_pk_).substr(0, 20)
        """
        import binascii
        pub_bytes = binascii.unhexlify(pubkey_hex.replace('0x', ''))
        hash_hex = sm3.sm3_hash(list(pub_bytes))
        return hash_hex[:40]
    
    def send_gmssl_transaction(self, pri_key_hex, pub_key_hex, to, step, amount=0, contract_code='', input_hex='', prefund=0):
        """
        Send GmSSL transaction: build message -> SM3 digest -> SM2 sign -> send
        """
        contract_code = normalize_hex(contract_code)
        input_hex = normalize_hex(input_hex)
        my_addr = self.get_gmssl_address(pub_key_hex)
        nonce_addr = to + my_addr if (step in [StepType.kContractExcute, StepType.kContractRefund]) else my_addr
        
        try:
            r = requests.post(self.query_url, data={"address": nonce_addr}, verify=self.verify_ssl).json()
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

        requests.post(self.gmssl_url, data=data, verify=self.verify_ssl)
        return txh_hex



# ===========================================================================
# EIP-1559 Transaction Support
# ===========================================================================

def _eth_rlp_encode_uint(v: int) -> bytes:
    """RLP-encode a uint."""
    if v == 0:
        return b'\x80'
    be = v.to_bytes((v.bit_length() + 7) // 8, 'big')
    if len(be) == 1 and be[0] < 0x80:
        return be
    return bytes([0x80 + len(be)]) + be


def _eth_rlp_encode_bytes(b: bytes) -> bytes:
    """RLP-encode a byte string."""
    if len(b) == 0:
        return b'\x80'
    if len(b) == 1 and b[0] < 0x80:
        return b
    if len(b) <= 55:
        return bytes([0x80 + len(b)]) + b
    len_be = len(b).to_bytes((len(b).bit_length() + 7) // 8, 'big')
    return bytes([0xb7 + len(len_be)]) + len_be + b


def _eth_rlp_list(payload: bytes) -> bytes:
    """RLP-encode a list from its concatenated payload."""
    if len(payload) <= 55:
        return bytes([0xc0 + len(payload)]) + payload
    len_be = len(payload).to_bytes((len(payload).bit_length() + 7) // 8, 'big')
    return bytes([0xf7 + len(len_be)]) + len_be + payload


def _eth_sign_and_send(client, pk_hex: str, to: bytes, value: int, data: bytes,
                       nonce: int, gas_limit: int = 5000000, gas_price: int = 1,
                       chain_id: int = 3355103125, use_eip1559: bool = False,
                       max_priority_fee_per_gas: int = None, max_fee_per_gas: int = None) -> str:
    """
    Build an EIP-155 (legacy) or EIP-1559 (Type 2) signed transaction, send via /eth JSON-RPC, return tx_hash hex.
    Uses eth_account for correct Ethereum-compatible signing.
    
    Args:
        client: Shardora client instance
        pk_hex: Private key in hex
        to: Recipient address (20 bytes)
        value: Value to transfer
        data: Transaction data
        nonce: Transaction nonce
        gas_limit: Gas limit
        gas_price: Gas price (for legacy transactions)
        chain_id: Chain ID
        use_eip1559: If True, use EIP-1559 (Type 2) transaction format
        max_priority_fee_per_gas: Max priority fee per gas (EIP-1559 only)
        max_fee_per_gas: Max fee per gas (EIP-1559 only)
    
    Returns:
        Transaction hash in hex format
    """
    from eth_account import Account
    from Crypto.Hash import keccak as _keccak

    # Build transaction dict
    from eth_utils import to_checksum_address as _to_ck
    
    if use_eip1559:
        # EIP-1559 (Type 2) transaction
        if max_priority_fee_per_gas is None:
            max_priority_fee_per_gas = gas_price
        if max_fee_per_gas is None:
            max_fee_per_gas = gas_price
            
        tx = {
            'type': 2,  # EIP-1559
            'chainId': chain_id,
            'nonce': nonce,
            'maxPriorityFeePerGas': max_priority_fee_per_gas,
            'maxFeePerGas': max_fee_per_gas,
            'gas': gas_limit,
            'to': _to_ck('0x' + to.hex()) if to else None,
            'value': value,
            'data': data,
            'accessList': [],  # Empty access list
        }
        print(f"  [DEBUG] Building EIP-1559 transaction: nonce={nonce}, maxFeePerGas={max_fee_per_gas}, "
              f"maxPriorityFeePerGas={max_priority_fee_per_gas}, gas={gas_limit}")
    else:
        # Legacy transaction
        tx = {
            'nonce': nonce,
            'gasPrice': gas_price,
            'gas': gas_limit,
            'value': value,
            'data': data,
            'chainId': chain_id,
        }
        if to:
            tx['to'] = _to_ck('0x' + to.hex())
        # If 'to' is absent → contract creation

    # Sign with eth_account — handles EIP-155, recovery_id, canonical s, etc.
    signed = Account.sign_transaction(tx, '0x' + pk_hex)
    raw_tx_bytes = getattr(signed, 'raw_transaction', None) or signed.rawTransaction
    raw_tx_hex = raw_tx_bytes.hex()
    print(f"  [DEBUG] raw_tx first bytes: {raw_tx_hex[:20]}... (len={len(raw_tx_bytes)})")

    # Compute and print the signing RLP for comparison with C++ side (Legacy only)
    if not use_eip1559:
        _sp = b''
        _sp += _eth_rlp_encode_uint(nonce)
        _sp += _eth_rlp_encode_uint(gas_price)
        _sp += _eth_rlp_encode_uint(gas_limit)
        _sp += _eth_rlp_encode_bytes(to)
        _sp += _eth_rlp_encode_uint(value)
        _sp += _eth_rlp_encode_bytes(data)
        _sp += _eth_rlp_encode_uint(chain_id)
        _sp += _eth_rlp_encode_uint(0)
        _sp += _eth_rlp_encode_uint(0)
        _srlp = _eth_rlp_list(_sp)
        _shash = _keccak.new(digest_bits=256).update(_srlp).digest()
        print(f"  [DEBUG] Python signing_rlp={_srlp.hex()}")
        print(f"  [DEBUG] Python signing_hash={_shash.hex()}")

    # Verify: recovered address should match our Shardora address
    expected_addr = client.get_address(pk_hex)
    recovered_addr = Account.recover_transaction(raw_tx_bytes).lower().replace('0x', '')
    # Also print the expected uncompressed pubkey for comparison with C++ side
    from ecdsa import SigningKey as _SK, SECP256k1 as _S
    _sk = _SK.from_string(bytes.fromhex(pk_hex), curve=_S)
    _pub_uncompressed_no_prefix = _sk.verifying_key.to_string().hex()  # 64 bytes
    print(f"  [DEBUG] expected pubkey (64B no prefix): {_pub_uncompressed_no_prefix}")
    print(f"  [DEBUG] expected_addr={expected_addr}, recovered_addr={recovered_addr}")
    if recovered_addr != expected_addr:
        print(f"  [WARN] Address mismatch! ETH recovery gives different address than Shardora.")

    # Send via /eth JSON-RPC
    import requests as _req
    rpc_url = f"{client.base_url}/eth"
    rpc_body = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "eth_sendRawTransaction",
        "params": [raw_tx_hex]  # no 0x prefix — C++ HexDecode doesn't expect it
    }
    resp = _req.post(rpc_url, json=rpc_body, verify=client.verify_ssl)
    result = resp.json()
    print(f"  [eth_sendRawTransaction] {result}")
    if "error" in result:
        raise RuntimeError(f"eth_sendRawTransaction failed: {result['error']}")
    return result.get("result", "")
