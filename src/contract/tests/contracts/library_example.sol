// SPDX-License-Identifier:MIT

pragma solidity ^0.8.7;

library LibraryDemo{
    struct structData{
        mapping(string=>uint32) map;
    }

    function insertData(structData storage self,string memory _name,uint32 _age)public {
        self.map[_name]=_age;
    }

    function get(structData storage self,string memory _name)view public returns(uint32){
        return self.map[_name];
    }
}