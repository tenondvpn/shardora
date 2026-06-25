// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * Minimal treasury that forwards value to ProbePool.sellSHARDORA.
 */
contract ProbeTreasury {
    address public pool;
    address public bridge;
    uint256 public totalSwaps;
    uint256 public lastOut;
    address public owner;

    constructor(address _pool) {
        pool = _pool;
        owner = msg.sender;
    }

    function setBridge(address _bridge) external {
        require(msg.sender == owner, "not owner");
        bridge = _bridge;
    }

    modifier onlyBridge() {
        require(msg.sender == bridge, "ProbeTreasury: not bridge");
        _;
    }

    function swap(uint256 minOut) external payable onlyBridge returns (uint256 out) {
        (bool ok, bytes memory ret) = pool.call{value: msg.value}(
            abi.encodeWithSignature("sellSHARDORA(uint256)", minOut)
        );
        require(ok, "ProbeTreasury: pool call failed");
        out = abi.decode(ret, (uint256));
        require(out > 0, "ProbeTreasury: zero out");
        totalSwaps += 1;
        lastOut = out;
    }
}

