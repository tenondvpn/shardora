// SPDX-License-Identifier:MIT

pragma solidity ^0.8.7;

import "./library_example.sol";

contract CallLib{
    LibraryDemo.structData private libObj;

    function insertName(string memory _name,uint32 _age)public{
        LibraryDemo.insertData(libObj,_name,_age);
    }

    function get(string memory _name)view public returns(uint32){
        return LibraryDemo.get(libObj,_name);
    }
}
