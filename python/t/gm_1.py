from __future__ import annotations
import secrets
import time
from eth_utils import to_checksum_address
import requests
import binascii
from gmssl import sm2, sm3, func

from shardora_sdk import ShardoraWeb3Mock, StepType, compile_and_link, get_sm2_public_key

# --- 5. Main Execution ---
PROBE_POOL_SOL = """
pragma solidity ^0.8.20;

contract ProbePool {
    uint256 public reserveSHARDORA;
    uint256 public reserveUSDC;

    event PoolSwap(address indexed sender, uint256 amountIn, uint256 amountOut, uint256 resSHARDORA, uint256 resUSDC);

    constructor(uint256 s, uint256 u) payable {
        reserveSHARDORA = s;
        reserveUSDC = u;
    }

    function sellSHARDORA(uint256 m) external payable returns (uint256 out) {
        out = (msg.value * reserveUSDC) / (reserveSHARDORA + msg.value);
        require(out >= m, 'ProbePool: slippage');

        reserveSHARDORA += msg.value;
        reserveUSDC -= out;

        emit PoolSwap(msg.sender, msg.value, out, reserveSHARDORA, reserveUSDC);
        return out;
    }
}
"""

PROBE_TREASURY_SOL = """
pragma solidity ^0.8.20;

contract ProbeTreasury {
    address public pool;
    address public bridge;
    uint256 public totalSwaps;

    event TreasuryForwarded(address indexed poolAddr, uint256 value, uint256 minOut);

    constructor(address p) payable {
        pool = p;
    }

    function setBridge(address b) external {
        bridge = b;
    }

    function swap(uint256 m) external payable returns (uint256 out) {
        require(msg.sender == bridge, 'ProbeTreasury: not bridge');

        emit TreasuryForwarded(pool, msg.value, m);

        (bool ok, bytes memory ret) = pool.call{value: msg.value}(
            abi.encodeWithSignature('sellSHARDORA(uint256)', m)
        );
        require(ok, 'ProbeTreasury: call sellSHARDORA failed');

        out = abi.decode(ret, (uint256));
        totalSwaps += 1;
        return out;
    }
}
"""

PROBE_BRIDGE_SOL = """
pragma solidity ^0.8.20;

contract ProbeBridge {
    address public treasury;
    uint256 public totalRequests;

    event BridgeRequest(address indexed user, uint256 value, uint256 minOut, uint256 requestId);

    constructor(address t) {
        treasury = t;
    }

    function request(uint256 m) external payable returns (uint256 out) {
        totalRequests += 1;
        emit BridgeRequest(msg.sender, msg.value, m, totalRequests);

        (bool ok, bytes memory ret) = treasury.call{value: msg.value}(
            abi.encodeWithSignature('swap(uint256)', m)
        );
        require(ok, 'ProbeBridge: call swap failed');

        out = abi.decode(ret, (uint256));
        return out;
    }
}
"""

RANDOM_SALT = secrets.token_hex(31)
def test_library_with_contrcat(w3, MY, KEY):
    print("\n--- TEST CASE 1: Library ---")
    src = "pragma solidity ^0.8.0; library MathLib { function add(uint a, uint b) public pure returns(uint){return a+b;} } contract Calculator { function use(uint a, uint b) public pure returns(uint){return MathLib.add(a,b);} }"
    l_bin, l_abi = compile_and_link(src, "MathLib")
    lib = w3.shardora.contract(abi=l_abi, bytecode=l_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '01', 'step': StepType.kCreateLibrary}, KEY)
    c_bin, c_abi = compile_and_link(src, "Calculator", libs={"MathLib": lib.address})
    calc = w3.shardora.contract(abi=c_abi, bytecode=c_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '02'}, KEY)
    print(f"Result: {calc.functions.use(10, 20).transact(KEY)['decoded_output']}")

def test_contract_call_contract(w3, MY, KEY):
    print("\n--- TEST CASE 3: Chain Call ---")
    p_bin, p_abi = compile_and_link(PROBE_POOL_SOL, "ProbePool")
    pool = w3.shardora.contract(abi=p_abi, bytecode=p_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '03', 'args': [10000, 10000], 'amount': 5000000 }, KEY)

    t_bin, t_abi = compile_and_link(PROBE_TREASURY_SOL, "ProbeTreasury")
    treasury = w3.shardora.contract(abi=t_abi, bytecode=t_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '04', 'args': [to_checksum_address(pool.address)], 'amount': 5000000 }, KEY)

    b_bin, b_abi = compile_and_link(PROBE_BRIDGE_SOL, "ProbeBridge")
    bridge = w3.shardora.contract(abi=b_abi, bytecode=b_bin, sender_address=MY).deploy({'from': MY, 'salt': RANDOM_SALT + '05', 'args': [to_checksum_address(treasury.address)]}, KEY)

    treasury.functions.setBridge(to_checksum_address(bridge.address)).transact(KEY)
    receipt = bridge.functions.request(1).transact(KEY, value=5)
    print(f"Chain Call Result (AmountOut): {receipt.get('decoded_output')}")
    if receipt.get('status') == 0:
        print(f"✅ Chain Call Success! AmountOut: {receipt.get('decoded_output')}")

        for e in receipt.get('decoded_events', []):
            print(f"🔔 Event Log: {e['event']} -> {e['args']}")
    else:
        print(f"❌ Chain Call Failed: {receipt.get('msg')}")

    print(f"Bridge Total Requests: {bridge.functions.totalRequests().call()}")

def test_transfer(w3, MY, KEY, dest):
    print("\n--- TEST CASE 2: Standard Transfer ---")
    # dest = "620a1c023fdef21f3c10bf3d468de37d5ecfdc7b"
    transfer_amount = 500000000
    
    balance_before = w3.client.get_balance(dest) # 1. Record balance before transfer
    print(f"Balance before: {balance_before}")
    
    receipt = w3.shardora.send_transaction({'to': dest, 'value': transfer_amount}, KEY) # 2. Execute transfer transaction
    
    if receipt.get('status') == 0: # 3. Verify transaction status
        print(f"Transfer Sent Successfully. Hash: {receipt.get('tx_hash', 'N/A')}")
        
        count = 0
        while count < 30:
            time.sleep(2) # Give the node some synchronization time (optional, depends on your RPC response speed)
            
            balance_after = w3.client.get_balance(dest) # 4. Get balance after transfer
            print(f"Balance after: {balance_after}")
            
            expected_balance = balance_before + transfer_amount
            if balance_after == expected_balance:
                print(f"✅ Balance Verification PASSED: {balance_before} + {transfer_amount} == {balance_after}")
                break
            else:
                print(f"❌ Balance Verification FAILED!")
                print(f"   Expected: {expected_balance}")
                print(f"   Actual:   {balance_after}")

            count += 1
    else:
        print(f"❌ Transfer Failed with status: {receipt.get('status')} | Msg: {receipt.get('msg')}")

def test_gmssl_transfer(w3, GM_KEY):
    """
    Test GmSSL standard transfer
    Utilizes SDK internal logic: passing gm_pubkey automatically switches between SM2/SM3
    """
    print("\n--- TEST CASE: GmSSL Standard Transfer ---")
    dest = "0000000000000000000000000000000000000001"
    
    gm_pubkey = get_sm2_public_key(GM_KEY)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey) # Call SDK internal method to calculate address (SM3 truncation)
    print(f"GmSSL Sender Address: {GM_MY}")

    # 2. Construct transaction dictionary
    tx_dict = {
        'to': dest,
        'value': 10000,
        'gm_pubkey': gm_pubkey
    }

    print("Sending GmSSL Transfer...")
    receipt = w3.shardora.send_gmssl_transaction(tx_dict, GM_KEY) # 3. Initiate transaction

    print(f"GmSSL Transfer Status: {receipt.get('status')}")
    if receipt.get('status') == 0:
        print(f"✅ Success! New balance: {w3.client.get_balance(dest)}")
    else:
        print(f"❌ Failed: {receipt.get('msg')}")

def test_gmssl_contract_flow(w3, GM_KEY):
    """
    Test the full contract flow for GmSSL accounts: Deploy -> Prefund Gas -> Call
    Fully utilizes gm_mode=True to automatically derive public key
    """
    print("\n--- TEST CASE: GmSSL Contract Full Flow (Auto-Derive) ---")

    src = """
    pragma solidity ^0.8.0;
    contract GmVault {
        uint256 public data;
        function store(uint256 v) public { data = v; }
    }
    """
    bin_code, abi = compile_and_link(src, "GmVault")
    
    # 2. Calculate Sender address for deploy parameters
    gm_pubkey = get_sm2_public_key(GM_KEY)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey)
    
    print(f"GmSSL Sender Address pk: {gm_pubkey}, GM_MY: {GM_MY}")
    # 3. Deploy contract
    print("[*] Deploying GmVault via GmSSL...")
    gm_vault = w3.shardora.contract(abi=abi, bytecode=bin_code)
    gm_vault.deploy({
        'from': GM_MY,
        'salt': secrets.token_hex(31) + 'gm_auto',
        'gm_pubkey': gm_pubkey,
        'gm_mode': True
    }, GM_KEY)

    if gm_vault.deploy_receipt.get('status') != 0:
        print(f"❌ Deploy Failed: {gm_vault.deploy_receipt.get('msg')}")
        return

    print(f"GmSSL Contract at: {gm_vault.address}")

    # 4. Prefund Gas
    print("[*] Setting Gas Prefund (gm_mode=True)...")
    gm_vault.prefund(50000000, GM_KEY, gm_mode=True)

    # 5. Call Contract (Transact)
    print("[*] Calling store(888) via SM2 (gm_mode=True)...")
    receipt = gm_vault.functions.store(888).transact(GM_KEY, gm_mode=True)

    if receipt.get('status') == 0:
        result = gm_vault.functions.data().call()
        print(f"✅ Success! Data in vault: {result}")
    else:
        print(f"❌ Call Failed: {receipt.get('msg')}")

def gmssl_sign_test():
    IP, PORT = "127.0.0.1", 23001
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address("71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6")
    test_transfer(
        w3, MY, 
        "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6", 
        "19b46cb80e027a99ab41d60e68b8a8a096f50869")
    
    GM_KEY = "c4b9e7a21d5f83c0a1e4d6b9f2a1e5c8d3b7a9f0e1d2c3b4a5968778695a4b3c"
    test_gmssl_transfer(w3, GM_KEY)
    test_gmssl_contract_flow(w3, GM_KEY)

if __name__ == "__main__":
    IP, PORT = "127.0.0.1", 23001
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address("71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6")
    test_transfer(
        w3, MY, 
        "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6", 
        "19b46cb80e027a99ab41d60e68b8a8a096f50869")
 