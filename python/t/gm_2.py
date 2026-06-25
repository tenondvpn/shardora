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

if __name__ == "__main__":
    IP, PORT = "127.0.0.1", 23001
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address("71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6")
    GM_KEY = "c4b9e7a21d5f83c0a1e4d6b9f2a1e5c8d3b7a9f0e1d2c3b4a5968778695a4b3c"
    test_gmssl_transfer(w3, GM_KEY)
 