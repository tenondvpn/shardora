// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0  <0.9.0;

import "./library_example.sol";  //�����

contract UseLibraryExample {

    using MyLibrary for uint256;

    function getSum(uint256 firstNumber, uint256 secondNumber) public pure returns(uint256) {
        return firstNumber.add(secondNumber); //���������ܲ���������
    }

}

