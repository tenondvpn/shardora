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

if __name__ == "__main__":
    IP, PORT = "127.0.0.1", 23001
    w3 = ShardoraWeb3Mock(IP, PORT)
    GM_KEY = "c4b9e7a21d5f83c0a1e4d6b9f2a1e5c8d3b7a9f0e1d2c3b4a5968778695a4b3c"
    test_gmssl_contract_flow(w3, GM_KEY)
