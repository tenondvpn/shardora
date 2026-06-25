// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * Minimal caller that forwards msg.value to ProbeCallee.hit() via low-level call.
 */
contract ProbeCaller {
    address public callee;
    uint256 public forwards;
    uint256 public lastForwardValue;

    constructor(address _callee) {
        callee = _callee;
    }

    function forwardHit() external payable returns (bool ok, bytes memory ret) {
        (ok, ret) = callee.call{value: msg.value}(abi.encodeWithSignature("hit()"));
        require(ok, "ProbeCaller: forward failed");
        forwards += 1;
        lastForwardValue = msg.value;
    }
}

