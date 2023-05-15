const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

console.log("test smart contract signature: ");
var account1 = web3.eth.accounts.privateKeyToAccount('0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
console.log("account1 :");
console.log(account1.address);
var account2 = web3.eth.accounts.privateKeyToAccount('0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
console.log("account2 :");
console.log(account2.address);
var account3 = web3.eth.accounts.privateKeyToAccount('0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
console.log("account3 :");
console.log(account3.address);

var cons_codes = web3.eth.abi.encodeParameters(['address[]'],
    [[account1.address,
        account2.address,
        account3.address]]);
console.log("cons_codes: " + cons_codes);


// func code
var ResAddFunc = web3.eth.abi.encodeFunctionSignature('ResAdd(bytes32,bytes,bytes)');
var ResAddFunc_param_codes = web3.eth.abi.encodeParameters(['bytes32', 'bytes', 'bytes'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5']);
console.log("ResAddFunc: " + ResAddFunc.substring(2) + ResAddFunc_param_codes.substring(2));

var AttrReg = web3.eth.abi.encodeFunctionSignature('AttrReg(bytes,bytes32,bytes[])');
var test_attr = "test_attr";
var test_attr_hash = web3.utils.keccak256(test_attr);

var sig1 = web3.eth.accounts.sign(test_attr_hash, '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
var sig2 = web3.eth.accounts.sign(test_attr_hash, '0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
var sig3 = web3.eth.accounts.sign(test_attr_hash, '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
var AttrReg_param_codes = web3.eth.abi.encodeParameters(['bytes', 'bytes32', 'bytes[]'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', test_attr_hash, [sig1.signature, sig2.signature, sig3.signature]]);
console.log("AttrReg: " + AttrReg.substring(2) + AttrReg_param_codes.substring(2));

var param_code_hash = web3.eth.accounts.hashMessage(test_attr_hash)
var recover1 = web3.eth.accounts.recover({
    messageHash: param_code_hash,
    v: sig1.v,
    r: sig1.r,
    s: sig1.s
});
console.log('recover1: ' + recover1);


var recover2 = web3.eth.accounts.recover({
    messageHash: param_code_hash,
    v: sig2.v,
    r: sig2.r,
    s: sig2.s
});
console.log('recover2: ' + recover2);

var recover3 = web3.eth.accounts.recover({
    messageHash: param_code_hash,
    v: sig3.v,
    r: sig3.r,
    s: sig3.s
});
console.log('recover3: ' + recover3);


