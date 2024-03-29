const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var abortCode = web3.eth.abi.encodeFunctionSignature('abort()');
console.log("abortCode function code: " + abortCode);

var confirmPurchaseCode = web3.eth.abi.encodeFunctionSignature('confirmPurchase()');
console.log("confirmPurchaseCode function code: " + confirmPurchaseCode);

var confirmReceivedCode = web3.eth.abi.encodeFunctionSignature('confirmReceived()');
console.log("confirmReceivedCode function code: " + confirmReceivedCode);

var refundSellerCode = web3.eth.abi.encodeFunctionSignature('refundSeller()');
console.log("refundSellerCode function code: " + refundSellerCode);

// params





