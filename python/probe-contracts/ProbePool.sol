// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * Minimal pool for Bridge->Treasury->Pool path diagnostics.
 */
contract ProbePool {
    uint256 public reserveSHARDORA;
    uint256 public reserveUSDC;
    uint256 public totalSells;
    uint256 public lastOut;

    constructor(uint256 _reserveSHARDORA, uint256 _reserveUSDC) {
        reserveSHARDORA = _reserveSHARDORA;
        reserveUSDC = _reserveUSDC;
    }

    function sellSHARDORA(uint256 minOut) external payable returns (uint256 out) {
        require(msg.value > 0, "ProbePool: zero in");
        require(reserveSHARDORA > 0 && reserveUSDC > 0, "ProbePool: empty");
        out = (msg.value * reserveUSDC) / (reserveSHARDORA + msg.value);
        require(out >= minOut, "ProbePool: slippage");
        reserveSHARDORA += msg.value;
        reserveUSDC -= out;
        totalSells += 1;
        lastOut = out;
    }
}

