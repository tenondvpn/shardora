// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

import "./ex_math_lib.sol";

contract Pkeet {
    using SafeMath for uint256;
    mapping(address => uint256) balances;

    mapping(bytes32 => address payable[]) public verifications;
    uint verification_valid_count;
    uint award_each;
    address payable public creator;
    constructor(uint valid_count, uint award) payable {
        uint256 b = 3;
        verification_valid_count = valid_count;
        award_each = award;
        creator = payable(msg.sender);
        balances[msg.sender] = award.add(b);
    }

    function callAbe(bytes memory params) public payable {
        bytes32 v1_hash = ripemd160(params);
        uint256 b = 3;
        balances[msg.sender] = balances[msg.sender].add(b);
        verifications[v1_hash].push(payable(msg.sender));
        if (verifications[v1_hash].length >= verification_valid_count) {
            for (uint i = 0; i < verification_valid_count; i++) {
                verifications[v1_hash][i].transfer(award_each);
            }

            selfdestruct(creator);
        }
    }
}
