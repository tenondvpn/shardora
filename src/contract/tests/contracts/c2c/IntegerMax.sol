pragma solidity ^0.6.8;  //不允许低于0.6.8 版本的编译器编译， 也不允许高于（包含） 0.7.0 版本的编译器编译

contract IntegerMax {
    uint256 public a;
    uint256 public b;
    
    function myTest() external {
        a = type(uint256).min;    //版本不能太低
        b = type(uint256).max;
    }
}