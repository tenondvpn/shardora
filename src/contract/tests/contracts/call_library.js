const Web3 = require('web3')
var web3;

if (typeof web3 !== 'undefined') {
    web3 = new Web3(web3.currentProvider);
} else {
    web3 = new Web3(new Web3.providers.HttpProvider("http://localhost:8545"));
}
/*
var address = "0xd77caeda977ae75924879da5057de4dfaafb63fb"
web3.eth.getBalance(address, (err, wei) => {

    // 余额单位从wei转换为ether
    balance = web3.utils.fromWei(wei, 'ether')
    console.log("balance: " + balance)
})
*/
var account = web3.eth.getAccounts()[0];
var accs = web3.eth.getAccounts();
var sha3Msg = web3.utils.sha3("blockchain");
//var signedData = web3.eth.sign(account, sha3Msg);
console.log("account 0: " + account)
console.log(sha3Msg)
console.log(accs)


//web3.eth.getAccounts(console.log);



var web3 = new Web3('http://localhost:8545');
// or
var web3 = new Web3(new Web3.providers.HttpProvider('http://localhost:8545'));

// change provider
web3.setProvider('ws://localhost:8546');
// or
web3.setProvider(new Web3.providers.WebsocketProvider('ws://localhost:8546'));

// Using the IPC provider in node.js
var net = require('net');
var web3 = new Web3('/Users/myuser/Library/Ethereum/geth.ipc', net); // mac os path
// or
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path
// on windows the path is: "\\\\.\\pipe\\geth.ipc"
// on linux the path is: "/users/myuser/.ethereum/geth.ipc"

var call_claimPaiment_func_code = web3.eth.abi.encodeFunctionSignature('insertName(string,uint32)');
var call_claimPaiment_param_codes = web3.eth.abi.encodeParameters(['string', 'uint32'], ['2643000000', '1']);
console.log("call: " + call_claimPaiment_func_code.substring(2) + call_claimPaiment_param_codes.substring(2));

