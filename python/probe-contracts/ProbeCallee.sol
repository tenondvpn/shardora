// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * Minimal callee for Shardora call{value} diagnostics.
 */
contract ProbeCallee {
    uint256 public totalHits;
    uint256 public lastValue;

    function hit() external payable returns (uint256) {
        totalHits += 1;
        lastValue = msg.value;
        return msg.value;
    }
}

