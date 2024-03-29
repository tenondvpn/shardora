// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0  <0.9.0;

library Balances {
    function move(mapping(address => uint256) storage balances, address from, address to, uint amount) public {
        require(balances[from] >= amount);
        require(balances[to] + amount >= balances[to]);
        balances[from] -= amount;
        balances[to] += amount;
    }
}

contract Token {
    mapping(address => uint256) balances;
    using Balances for *;   // 引入库
    mapping(address => mapping (address => uint256)) allowed;
    address payable public sender;

    event Transfer(address from, address to, uint amount);
    event Approval(address owner, address spender, uint amount);

    constructor() payable {
        sender = payable(msg.sender);
        balances[sender] = msg.value;
    }

    function balanceOf(address tokenOwner) public view returns (uint balance) {
        return balances[tokenOwner];
    }
    function transfer(address to, uint amount) public returns (bool success) {
        balances.move(msg.sender, to, amount);
        emit Transfer(msg.sender, to, amount);
        return true;

    }

    function transferFrom(address from, address to, uint amount) public returns (bool success) {
        require(allowed[msg.sender][from] >= amount);
        allowed[msg.sender][from] -= amount;
        balances.move(from, to, amount);   // 使用了库方法
        emit Transfer(from, to, amount);
        return true;
    }

    function approve(address spender, uint tokens) public returns (bool success) {
        require(allowed[msg.sender][spender] == 0, "");
        allowed[msg.sender][spender] = tokens;
        emit Approval(msg.sender, spender, tokens);
        return true;
    }
}