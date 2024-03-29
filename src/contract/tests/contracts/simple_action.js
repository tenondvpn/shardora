const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var bidCode = web3.eth.abi.encodeFunctionSignature('bid()');
console.log("bidCode function code: " + bidCode);

var withdrawCode = web3.eth.abi.encodeFunctionSignature('withdraw()');
console.log("withdrawCode function code: " + withdrawCode);

var auctionEndCode = web3.eth.abi.encodeFunctionSignature('auctionEnd()');
console.log("auctionEndCode function code: " + auctionEndCode);

// params
var contriuctParam = web3.eth.abi.encodeParameters(['uint256', 'address'], ['100000000', '0x544064949151817a1185e931ea43a71493f9f33c']);
console.log("contriuctParam: " + contriuctParam);



