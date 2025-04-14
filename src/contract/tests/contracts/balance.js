const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var createFunc = web3.eth.abi.encodeFunctionSignature('create(address,uint256)');
var createParam = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '52342343999']);
console.log("create params: " + createFunc.substring(2) + createParam.substring(2));
var createParam1 = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x15518b7643b094a6b1faba3a91fc16c20a9041da', '52342343999']);
console.log("create params: " + createFunc.substring(2) + createParam1.substring(2));

var balanceofFunc = web3.eth.abi.encodeFunctionSignature('balanceOf(address)');
var balanceofParam = web3.eth.abi.encodeParameters(['address'], ['0x544064949151817a1185e931ea43a71493f9f33c']);
console.log("balance of params: " + balanceofFunc.substring(2) + balanceofParam.substring(2));

var transferFunc = web3.eth.abi.encodeFunctionSignature('transfer(address,uint256)');
var transferParam = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '52342343']);
console.log("transfer params: " + transferFunc.substring(2) + transferParam.substring(2));

var transferParam1 = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x15518b7643b094a6b1faba3a91fc16c20a9041da', '523423431']);
console.log("transfer params1: " + transferFunc.substring(2) + transferParam1.substring(2));


var transferFromFunc = web3.eth.abi.encodeFunctionSignature('transfer(address,address,uint256)');
var transferFromParam = web3.eth.abi.encodeParameters(['address', 'address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '0x15518b7643b094a6b1faba3a91fc16c20a9041da', '523423']);
console.log("transfer from params: " + transferFromFunc.substring(2) + transferFromParam.substring(2));

var approveFunc = web3.eth.abi.encodeFunctionSignature('approve(address,uint256)');
var approveParam = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '523423439']);
console.log("approve params: " + approveFunc.substring(2) + approveParam.substring(2));

var approveParam1 = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x15518b7643b094a6b1faba3a91fc16c20a9041da', '523423439']);
console.log("approve params: " + approveFunc.substring(2) + approveParam1.substring(2));

