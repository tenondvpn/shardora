pragma solidity ^0.8.0;
 
contract MySmartContract {
    address public contractOwner;
    uint public amount;

    // 构造函数
    constructor()  { 
        contractOwner = msg.sender;  
    }
    
    //将币转到该合约
    function sendMoneyToContract()  public payable {// 接收eth
        amount += msg.value;
        //payable(address(this)).transfer(msg.value);  //这种写法 有异常
    }
    
    //获取合约中币数量
    function getBalance()  public view returns (uint) {
        return address(this).balance;
    }

    //将合约中所有的币转到一个账户地址
    function withdrawAll(address payable _to) public{
        require(contractOwner == _to);
        _to.transfer(address(this).balance);
    }

    //trigger recevier function
    //纯转账调用receiver回退函数，例如对每个空empty calldata的调用
    //对函数方法的调用 出发receiver回退函数 
    function CallTransTest()payable public{
        (bool success,) = address(this).call(abi.encodeWithSignature("transderToContract()"));
            //emit TransEvent(address(this),2);
    }
    
    //trigger fallback function
    // 除了纯转账外，所有的调用都会调用这个函数．
    // (因为除了 receive 函数外，没有其他的函数).
    // 任何对合约非空calldata 调用会执行回退函数(即使是调用函数附加以太).
    function CallNoexistTest()payable public{
        (bool success,) = address(this).call(abi.encodeWithSignature("noFunction()"));
            //emit TransEvent(address(this),2);
    }
    
    fallback() external payable {
                emit TransEvent(address(this),1);
    }

    receive() external payable {
            emit TransEvent(address(this),2);
    }

    // 向合约账户转账 ev
    event TransEvent(address,uint);
}