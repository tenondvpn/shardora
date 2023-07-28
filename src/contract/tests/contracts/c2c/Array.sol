// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.6.0 <0.9.0;

contract Array {
   function ff() public pure returns (uint[] memory){
        uint[] memory x = new uint[](4);
        x[0] = 1;
        x[1] = 3;
        x[2] = 4;
        x[3] = 5;

        return x;
    }

    function f() public pure {
        g([uint(1), 2, 3]); //g([1, 2, 3]);  //这样是异常
    }
    function g(uint[3] memory) public pure {
        // ...
    }

    function f2() public {
        //uint[] memory x = [uint(1), 3, 4]; //这一行引发了一个类型错误，因为 unint[3] memory 不能转换成 uint[] memory。
    }


    
 
}
