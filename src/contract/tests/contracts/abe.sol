// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

contract Abe {
    bytes32 test_ripdmd;

    constructor() payable {}

    function callAbe(bytes memory params) public payable {
        test_ripdmd = ripemd160(params);
    }
}
