const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var balanceOfFunc = web3.eth.abi.encodeFunctionSignature('balanceOf(address)');
var balanceOfParam = web3.eth.abi.encodeParameters(['address'], ['0x544064949151817a1185e931ea43a71493f9f33c']);
console.log("balanceOfParam: " + balanceOfFunc.substring(2) + balanceOfParam.substring(2));
var balanceOfParam2 = web3.eth.abi.encodeParameters(['address'], ['0x15518b7643b094a6b1faba3a91fc16c20a9041da']);
console.log("balanceOfParam2: " + balanceOfFunc.substring(2) + balanceOfParam2.substring(2));

var transferFunc = web3.eth.abi.encodeFunctionSignature('transfer(address,uint256)');
var transferParam = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '3453445']);
console.log("transferParam: " + transferFunc.substring(2) + transferParam.substring(2));
var transferParam1 = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x15518b7643b094a6b1faba3a91fc16c20a9041da', '3453445']);
console.log("transferParam1: " + transferFunc.substring(2) + transferParam1.substring(2));

var transferFromFunc = web3.eth.abi.encodeFunctionSignature('transferFrom(address,address,uint256)');
var transferFromParam = web3.eth.abi.encodeParameters(['address', 'address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '0x15518b7643b094a6b1faba3a91fc16c20a9041da', '3453445']);
console.log("transferFromParam: " + transferFromFunc.substring(2) + transferFromParam.substring(2));

var approveFunc = web3.eth.abi.encodeFunctionSignature('approve(address,uint256)');
var approveParam = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x544064949151817a1185e931ea43a71493f9f33c', '99999999']);
console.log("approveParam: " + approveFunc.substring(2) + approveParam.substring(2));
var approveParam1 = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0x15518b7643b094a6b1faba3a91fc16c20a9041da', '99999999']);
console.log("approveParam1: " + approveFunc.substring(2) + approveParam1.substring(2));
