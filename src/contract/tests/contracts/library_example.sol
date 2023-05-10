// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0  <0.9.0;

library MyLibrary {
    function add(uint256 a, uint256 b) public pure returns(uint256) {
        return a + b;
    }
}