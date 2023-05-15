const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

console.log("test smart contract signature: ");
var account1 = web3.eth.accounts.privateKeyToAccount('0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
console.log("account1 :");
console.log(account2);
var account2 = web3.eth.accounts.privateKeyToAccount('0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
console.log("account2 :");
console.log(account2);
var account3 = web3.eth.accounts.privateKeyToAccount('0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
console.log("account3 :");
console.log(account3);

var cons_codes = web3.eth.abi.encodeParameters(['address[]'],
    [['0xb8CE9ab6943e0eCED004cDe8e3bBed6568B2Fa01',
        '0xb8CE9ab6943e0eCED004cDe8e3bBed6568B2Fa02',
        '0xb8CE9ab6943e0eCED004cDe8e3bBed6568B2Fa03']]);
console.log("cons_codes: " + cons_codes);


var param_codes = web3.eth.abi.encodeParameters(['address', 'uint256', 'address'], ['0xb8CE9ab6943e0eCED004cDe8e3bBed6568B2Fa01', '10000000', '0x22c27146d7d240a92f29283407609ce6e00fe1d5']);
console.log("param_codes: " + param_codes);
var kek256 = web3.utils.keccak256(param_codes);
console.log("kek256: " + kek256);
var param_code_hash = web3.eth.accounts.hashMessage(kek256)
console.log("param_code_hash: " + param_code_hash)
var sig_param = web3.eth.accounts.sign(kek256, '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
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


