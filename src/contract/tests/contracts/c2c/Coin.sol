// SPDX-License-Identifier: GPL-3.0
pragma solidity ^0.8.4;

contract Coin {
    address  public  minter;   //定义一个状态变量，命名为“铸币者”
    mapping (address => uint) public balances; //存储每个人的币数量

    //构造函数，当且仅当合约创建时运行，把合约创建者赋值给“铸币者”
    constructor() {
        minter = msg.sender; //msg.sender代表合约创建者 ，不需要入参显示传入
    }

    //“铸币者”给receiver地址生成amount的币，即币无中生有的
    function mint(address receiver, uint amount) public {
        require(msg.sender == minter);   //铸币者只能是合约创建者，不允许所有人都能造币
        balances[receiver] += amount;
    }

    //调用者给receiver地址转币，数量为amount
    function send(address receiver, uint amount) public { //0x082B6aC9e47d7D83ea3FaBbD1eC7DAba9D687b36
        balances[msg.sender] -= amount;
        balances[receiver] += amount;
    }
}