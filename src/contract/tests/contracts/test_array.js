const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var func = web3.eth.abi.encodeFunctionSignature('testArrayRemove()');
console.log("param: " + func.substring(2) + "00000000000000000000000000000000000000000000000000000000");
