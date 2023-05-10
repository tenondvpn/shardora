// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0  <0.9.0;

import "./library_example.sol";  //�����

contract UseLibraryExample {

    using MyLibrary for uint;

    function getSum(uint firstNumber, uint secondNumber) public pure returns(uint) {
        return firstNumber.add(secondNumber); //���������ܲ���������
    }

}

