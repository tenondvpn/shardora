const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path
const { randomBytes } = require('crypto')
const Secp256k1 = require('./secp256k1_1')
const keccak256 = require('keccak256')
var querystring = require('querystring');
var http = require('http');
var fs = require('fs');

var self_private_key = null;
var self_public_key = null;
var local_count_shard_id = 3;
var contract_address = null;

function str_to_hex(str) {
    var arr1 = [];
    for (var n = 0; n < str.length; n++) {
        var hex = Number(str.charCodeAt(n)).toString(16);
        arr1.push(hex);
    }
    return arr1.join('');
}

function hexToBytes(hex) {
    for (var bytes = [], c = 0; c < hex.length; c += 2)
        bytes.push(parseInt(hex.substr(c, 2), 16));
    return bytes;
}

function init_private_key() {
    const privateKeyBuf = Secp256k1.uint256("fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971", 16)
    self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
    self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
    var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
    var address = keccak256(pk_bytes).toString('hex')
    console.log("self_account_id: " + address.toString('hex'));
    address = address.slice(address.length - 40, address.length)
    self_account_id = address;
    contract_address = fs.readFileSync('contract_address', 'utf-8');
    console.log("contract_address: " + contract_address);
}

function PostCode(data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '8781',
        path: '/transaction',
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': Buffer.byteLength(post_data)
        }
    };

    var post_req = http.request(post_options, function (res) {
        res.setEncoding('utf8');
        res.on('data', function (chunk) {
            if (chunk != "ok") {
                console.log('Response: ' + chunk + ", " + data);
            } else {
                console.log('Response: ' + chunk + ", " + data);
            }
        })
    });

    //console.log("req data: " + post_data);
    post_req.write(post_data);
    post_req.end();
}

function GetValidHexString(uint256_bytes) {
    var str_res = uint256_bytes.toString(16)
    while (str_res.length < 64) {
        str_res = "0" + str_res;
    }

    return str_res;
}

function create_contract(gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var tx_type = 6;
    var frompk = '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16);
    const MAX_UINT32 = 0xFFFFFFFF;
    var amount_buf = new Buffer(8);
    var big = ~~(amount / MAX_UINT32)
    var low = (amount % MAX_UINT32) - big
    amount_buf.writeUInt32LE(big, 4)
    amount_buf.writeUInt32LE(low, 0)

    var gas_limit_buf = new Buffer(8);
    var big = ~~(gas_limit / MAX_UINT32)
    var low = (gas_limit % MAX_UINT32) - big
    gas_limit_buf.writeUInt32LE(big, 4)
    gas_limit_buf.writeUInt32LE(low, 0)

    var gas_price_buf = new Buffer(8);
    var big = ~~(gas_price / MAX_UINT32)
    var low = (gas_price % MAX_UINT32) - big
    gas_price_buf.writeUInt32LE(big, 4)
    gas_price_buf.writeUInt32LE(low, 0)
    var step_buf = new Buffer(8);
    step_buf.writeUInt32LE(0, 0)
    step_buf.writeUInt32LE(0, 0)

    var prepay_buf = new Buffer(8);
    var big = ~~(prepay / MAX_UINT32)
    var low = (prepay % MAX_UINT32) - big
    prepay_buf.writeUInt32LE(big, 4)
    prepay_buf.writeUInt32LE(low, 0)

    const message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, Buffer.from(contract_bytes, 'hex'), Buffer.from(input, 'hex'), prepay_buf]);
    //var arrByte = Uint8Array.from(message_buf)
    var kechash = keccak256(message_buf)

    var digest = Secp256k1.uint256(kechash, 16)
    const sig = Secp256k1.ecsign(self_private_key, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(self_public_key.x, 16)
    const pubY = Secp256k1.uint256(self_public_key.y, 16)
    const isValidSig = Secp256k1.ecverify(pubX, pubY, sigR, sigS, digest)
    console.log("digest: " + digest)
    console.log("sigr: " + sigR.toString(16))
    console.log("sigs: " + sigS.toString(16))
    if (!isValidSig) {
        console.log('signature transaction failed.')
        return;
    }

    return {
        'gid': gid,
        'pubkey': '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': local_count_shard_id,
        'hash': kechash,
        'attrs_size': 4,
        "bytes_code": contract_bytes,
        "input": input,
        "pepay": prepay,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
    }
}

function create_tx(to, amount, gas_limit, gas_price) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var tx_type = 0;
    var frompk = '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16);
    const MAX_UINT32 = 0xFFFFFFFF;
    var amount_buf = new Buffer(8);
    var big = ~~(amount / MAX_UINT32)
    var low = (amount % MAX_UINT32) - big
    amount_buf.writeUInt32LE(big, 4)
    amount_buf.writeUInt32LE(low, 0)

    var gas_limit_buf = new Buffer(8);
    var big = ~~(gas_limit / MAX_UINT32)
    var low = (gas_limit % MAX_UINT32) - big
    gas_limit_buf.writeUInt32LE(big, 4)
    gas_limit_buf.writeUInt32LE(low, 0)

    var gas_price_buf = new Buffer(8);
    var big = ~~(gas_price / MAX_UINT32)
    var low = (gas_price % MAX_UINT32) - big
    gas_price_buf.writeUInt32LE(big, 4)
    gas_price_buf.writeUInt32LE(low, 0)
    var step_buf = new Buffer(8);
    step_buf.writeUInt32LE(0, 0)
    step_buf.writeUInt32LE(0, 0)

    const message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf]);
    var kechash = keccak256(message_buf)
    var digest = Secp256k1.uint256(kechash, 16)
    const sig = Secp256k1.ecsign(self_private_key, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(self_public_key.x, 16)
    const pubY = Secp256k1.uint256(self_public_key.y, 16)
    return {
        'gid': gid,
        'pubkey': '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': local_count_shard_id,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
    }
}

function new_contract(contract_bytes) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var kechash = keccak256(self_account_id + gid + contract_bytes).toString('hex')
    var self_contract_address = kechash.slice(kechash.length - 40, kechash.length)
    var data = create_contract(
        gid,
        self_contract_address,
        0,
        10000000,
        1,
        contract_bytes,
        "",
        999999999);
    PostCode(data);

    const opt = { flag: 'w', }
    fs.writeFile('contract_address', self_contract_address, opt, (err) => {
        if (err) {
            console.error(err)
        }
    })
}

function do_transaction(to_addr, amount, gas_limit, gas_price) {
    var data = create_tx(to_addr, amount, gas_limit, gas_price);
    PostCode(data);
}

function CreatePhr() {
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
}

init_private_key();
const args = process.argv.slice(2)
if (args[0] == 0) {
    do_transaction(args[1], 100000, 100000, 1);
}

if (args[0] == 1) {
    new_contract("60806040526102ef806100136000396000f3fe60806040526004361061001e5760003560e01c80634162d68f14610023575b600080fd5b61003d600480360381019061003891906101e8565b61003f565b005b60038160405161004f91906102a2565b602060405180830381855afa15801561006c573d6000803e3d6000fd5b5050506040515160601b6bffffffffffffffffffffffff191660008190555050565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b6100f5826100ac565b810181811067ffffffffffffffff82111715610114576101136100bd565b5b80604052505050565b600061012761008e565b905061013382826100ec565b919050565b600067ffffffffffffffff821115610153576101526100bd565b5b61015c826100ac565b9050602081019050919050565b82818337600083830152505050565b600061018b61018684610138565b61011d565b9050828152602081018484840111156101a7576101a66100a7565b5b6101b2848285610169565b509392505050565b600082601f8301126101cf576101ce6100a2565b5b81356101df848260208601610178565b91505092915050565b6000602082840312156101fe576101fd610098565b5b600082013567ffffffffffffffff81111561021c5761021b61009d565b5b610228848285016101ba565b91505092915050565b600081519050919050565b600081905092915050565b60005b8381101561026557808201518184015260208101905061024a565b60008484015250505050565b600061027c82610231565b610286818561023c565b9350610296818560208601610247565b80840191505092915050565b60006102ae8284610271565b91508190509291505056fea26469706673582212208b25ce5a394463f5a84735f017ca3d7d2a5d2768499dc22df0ba76e5086fe52464736f6c63430008110033");
}
