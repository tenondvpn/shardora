const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

console.log("test smart contract signature: ");
var account1 = web3.eth.accounts.privateKeyToAccount('0x348ce564d427a3311b6536bbcff9390d69395b06ed6c486954e971d960fe8709'); 
console.log("from :");
console.log(account1);
var param_codes = web3.eth.abi.encodeParameters(['address', 'uint256', 'address'], ['0xb8ce9ab6943e0eced004cde8e3bbed6568b2fa01', '10000000', '0x22c27146d7d240a92f29283407609ce6e00fe1d5']);
console.log("param_codes: " + param_codes);
var kek256 = web3.utils.keccak256(param_codes);
console.log("kek256: " + kek256);
var param_code_hash = web3.eth.accounts.hashMessage(kek256)
console.log("param_code_hash: " + param_code_hash)
var sig_param = web3.eth.accounts.sign(kek256, '0x348ce564d427a3311b6536bbcff9390d69395b06ed6c486954e971d960fe8709');
console.log("sig params: ");
console.log(sig_param);
var recover = web3.eth.accounts.recover({
    messageHash: param_code_hash,
    v: sig_param.v,
    r: sig_param.r,
    s: sig_param.s
});
console.log('recover: ' + recover);


// func code
var closeFunc = web3.eth.abi.encodeFunctionSignature('close(uint256,bytes)');
console.log("closeFunc function code: " + closeFunc);

var extendFunc = web3.eth.abi.encodeFunctionSignature('extend(uint256)');
console.log("extendFunc function code: " + extendFunc);

var claimTimeoutFunc = web3.eth.abi.encodeFunctionSignature('claimTimeout()');
console.log("claimTimeoutFunc function code: " + claimTimeoutFunc);

// params code
var constructerCode = web3.eth.abi.encodeParameters(['address', 'uint256'], ['0xdc09e1166271813aac21ff255960dcf39ccc000b', '100']);
console.log("constructerCode: " + constructerCode);

var closeCode = web3.eth.abi.encodeParameters(['uint256', 'bytes'], ['10000000', sig_param.signature]);
console.log("closeCode: " + closeCode);

var extendCode = web3.eth.abi.encodeParameters(['uint256'], ['100']);
console.log("extendCode: " + extendCode);


