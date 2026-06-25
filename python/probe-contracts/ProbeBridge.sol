// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * Minimal bridge that forwards value to ProbeTreasury.swap.
 */
contract ProbeBridge {
    address public treasury;
    uint256 public totalRequests;
    uint256 public lastOut;

    constructor(address _treasury) {
        treasury = _treasury;
    }

    function request(uint256 minOut) external payable returns (uint256 out) {
        require(msg.value > 0, "ProbeBridge: zero value");
        (bool ok, bytes memory ret) = treasury.call{value: msg.value}(
            abi.encodeWithSignature("swap(uint256)", minOut)
        );
        require(ok, "ProbeBridge: treasury call failed");
        out = abi.decode(ret, (uint256));
        require(out > 0, "ProbeBridge: zero out");
        totalRequests += 1;
        lastOut = out;
    }
}

