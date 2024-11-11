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
var self_private_key2 = null;
var self_public_key = null;
var self_public_keyi2 = null;
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
    //const privateKeyBuf = Secp256k1.uint256("20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 16) // e252d01a37b85e2007ed3cc13797aa92496204a4
    const privateKeyBuf = Secp256k1.uint256("d5a4758b94d34da11f818efbbc7b6739949aa7cb249c9403022b4ed54fa7b0a8", 16)
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
        port: '8301',
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

function param_contract(tx_type, gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay, key, value) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
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
    var big = ~~(tx_type / MAX_UINT32)
    var low = (tx_type % MAX_UINT32) - big
    step_buf.writeUInt32LE(big, 0)
    step_buf.writeUInt32LE(low, 0)

    var prepay_buf = new Buffer(8);
    var big = ~~(prepay / MAX_UINT32)
    var low = (prepay % MAX_UINT32) - big
    prepay_buf.writeUInt32LE(big, 4)
    prepay_buf.writeUInt32LE(low, 0)

    var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, Buffer.from(contract_bytes, 'hex'), 
        Buffer.from(input, 'hex'), prepay_buf, Buffer.from(key), Buffer.from(value)]);
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

    console.log("key: " + key);
    console.log("value: " + value);
    console.log("message_buf: " + message_buf);
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
        'key': key,
        'val': value,
        'attrs_size': 4,
        "bytes_code": contract_bytes,
        "input": input,
        "pepay": prepay,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
    }
}

function param_contract2(tx_type, gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var frompk = '04' + self_public_key2.x.toString(16) + self_public_key2.y.toString(16);
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
    var big = ~~(tx_type / MAX_UINT32)
    var low = (tx_type % MAX_UINT32) - big
    step_buf.writeUInt32LE(big, 0)
    step_buf.writeUInt32LE(low, 0)

    var prepay_buf = new Buffer(8);
    var big = ~~(prepay / MAX_UINT32)
    var low = (prepay % MAX_UINT32) - big
    prepay_buf.writeUInt32LE(big, 4)
    prepay_buf.writeUInt32LE(low, 0)

    var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, Buffer.from(contract_bytes, 'hex'), Buffer.from(input, 'hex'), prepay_buf]);
    var kechash = keccak256(message_buf)

    var digest = Secp256k1.uint256(kechash, 16)
    const sig = Secp256k1.ecsign(self_private_key2, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(self_public_key2.x, 16)
    const pubY = Secp256k1.uint256(self_public_key2.y, 16)
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
        'pubkey': '04' + self_public_key2.x.toString(16) + self_public_key2.y.toString(16),
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


function create_tx(to, amount, gas_limit, gas_price, prepay, tx_type) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
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
    var big = ~~(tx_type / MAX_UINT32)
    var low = (tx_type % MAX_UINT32) - big
    step_buf.writeUInt32LE(big, 0)
    step_buf.writeUInt32LE(low, 0)
    var prepay_buf = new Buffer(8);
    var big = ~~(prepay / MAX_UINT32)
    var low = (prepay % MAX_UINT32) - big
    prepay_buf.writeUInt32LE(big, 4)
    prepay_buf.writeUInt32LE(low, 0)

    var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, prepay_buf]);
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
        'pepay': prepay
    }
}

function create_tx2(to, amount, gas_limit, gas_price, prepay, tx_type) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var frompk = '04' + self_public_key2.x.toString(16) + self_public_key2.y.toString(16);
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
    var big = ~~(tx_type / MAX_UINT32)
    var low = (tx_type % MAX_UINT32) - big
    step_buf.writeUInt32LE(big, 0)
    step_buf.writeUInt32LE(low, 0)
    var prepay_buf = new Buffer(8);
    var big = ~~(prepay / MAX_UINT32)
    var low = (prepay % MAX_UINT32) - big
    prepay_buf.writeUInt32LE(big, 4)
    prepay_buf.writeUInt32LE(low, 0)

    var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, prepay_buf]);
    var kechash = keccak256(message_buf)
    var digest = Secp256k1.uint256(kechash, 16)
    const sig = Secp256k1.ecsign(self_private_key2, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(self_public_key2.x, 16)
    const pubY = Secp256k1.uint256(self_public_key2.y, 16)
    return {
        'gid': gid,
        'pubkey': '04' + self_public_key2.x.toString(16) + self_public_key2.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': local_count_shard_id,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
        'pepay': prepay
    }
}

function new_contract(contract_bytes) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var kechash = keccak256(self_account_id + gid + contract_bytes).toString('hex')
    var self_contract_address = kechash.slice(kechash.length - 40, kechash.length)
    var key = Buffer.from("__csc").toString('hex');
    var tmp_value = fs.readFileSync('data_auth.sol', 'utf-8');
    var value = Buffer.from(tmp_value).toString('hex');
    console.log("src key: " + key);
    console.log("src value: " + value);
    var data = param_contract(
        6,
        gid,
        self_contract_address,
        0,
        10000000,
        1,
        contract_bytes,
        "",
        999999999, key, value);
    PostCode(data);

    const opt = { flag: 'w', }
    fs.writeFile('contract_address', self_contract_address, opt, (err) => {
        if (err) {
            console.error(err)
        }
    })
}

function call_contract(input, amount) {
    contract_address = fs.readFileSync('contract_address', 'utf-8');
    console.log("contract_address: " + contract_address);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = param_contract(
        8,
        gid,
        contract_address,
        amount,
        9000000,
        1,
        "",
        input,
        0, "", "");
    PostCode(data);
}

function call_contract2(input, amount) {
    contract_address = fs.readFileSync('contract_address', 'utf-8');
    console.log("contract_address: " + contract_address);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = param_contract2(
        8,
        gid,
        contract_address,
        amount,
        9000000,
        1,
        "",
        input,
        0);
    PostCode(data);
}


function do_transaction(to_addr, amount, gas_limit, gas_price) {
    var data = create_tx(to_addr, amount, gas_limit, gas_price, 0, 0);
    PostCode(data);
}

function CreatePhr() {
    console.log("test smart contract signature: ");
    var account1 = web3.eth.accounts.privateKeyToAccount('0x4b07a853acfa8ebd71fd1585dd02289ec983bd125bbb6a5316c8015562a22e82');
    console.log("account1 :");
    console.log(account1.address.toLowerCase());
    var account2 = web3.eth.accounts.privateKeyToAccount('0x3701d2b9951b41390aaf198bb3fe096c4c8b5f3697b577fd06c350bbca2dfa5b');
    console.log("account2 :");
    console.log(account2.address.toLowerCase());
    var account3 = web3.eth.accounts.privateKeyToAccount('0x078ae19446ed495f90b204676b4540f73bba4ca1c3af1c57c1b2aa4ca06c7a12');
    console.log("account3 :");
    console.log(account3.address.toLowerCase());

    var cons_codes = web3.eth.abi.encodeParameters(['address[]'],
        [[account1.address,
        account2.address,
            account3.address]]);
    console.log("cons_codes: " + cons_codes.substring(2));

    {
        var func = web3.eth.abi.encodeFunctionSignature('AddManager(address[])');
        var funcParam = web3.eth.abi.encodeParameters(['address[]'], [[account1.address,
            account2.address,
                account3.address]]);
        console.log("AddManager func: " + func.substring(2) + funcParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('RemoveManager(address[])');
        var funcParam = web3.eth.abi.encodeParameters(['address[]'], [[account1.address]]);
        console.log("RemoveManager func: " + func.substring(2) + funcParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('Authorization(bytes)');
        var funcParam = web3.eth.abi.encodeParameters(['bytes'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5']);
        console.log("Authorization func: " + func.substring(2) + funcParam.substring(2));
    }
    
    {
        var func = web3.eth.abi.encodeFunctionSignature('GetAuthJson()');
        console.log("GetAuthJson func: " + func.substring(2));
    }
}

CreatePhr();

function GetConstructorParams(args) {
    if (args.length < 2) {
        return;
    }

    var addrs = [];
    for (var i = 1; i < args.length; ++i) {
        if (args[i].length != 42) {
            return null;
        }

        if (!args[i].startsWith("0x")) {
            return null;
        }

        addrs.push(args[i]);
    }
    
    var cons_codes = web3.eth.abi.encodeParameters(['address[]'], [addrs]);
    console.log("cons_codes: " + cons_codes.substring(2));
    return cons_codes.substring(2);
}

function GetAddManagerParams(args) {
    if (args.length < 2) {
        return;
    }

    var addrs = [];
    for (var i = 1; i < args.length; ++i) {
        if (args[i].length != 42) {
            return null;
        }

        if (!args[i].startsWith("0x")) {
            return null;
        }

        addrs.push(args[i]);
    }
    var func = web3.eth.abi.encodeFunctionSignature('AddManager(address[])');
    var funcParam = web3.eth.abi.encodeParameters(['address[]'], [addrs]);
    console.log("AddManager func: " + func.substring(2) + funcParam.substring(2));
    return func.substring(2) + funcParam.substring(2);
}

function GetRemoveManagerParams(args) {
    if (args.length < 2) {
        return;
    }

    var addrs = [];
    for (var i = 1; i < args.length; ++i) {
        if (args[i].length != 42) {
            return null;
        }

        if (!args[i].startsWith("0x")) {
            return null;
        }

        addrs.push(args[i]);
    }
    
    var func = web3.eth.abi.encodeFunctionSignature('RemoveManager(address[])');
    var funcParam = web3.eth.abi.encodeParameters(['address[]'], [addrs]);
    console.log("RemoveManager func: " + func.substring(2) + funcParam.substring(2));
    return func.substring(2) + funcParam.substring(2);
}

function GetAuthorizationParams(args) {
    if (args.length < 2) {
        return;
    }

    var func = web3.eth.abi.encodeFunctionSignature('Authorization(bytes)');
    var funcParam = web3.eth.abi.encodeParameters(['bytes'], ['0x' + str_to_hex(args[1])]);
    console.log("Authorization func: " + func.substring(2) + funcParam.substring(2));
    return func.substring(2) + funcParam.substring(2);
}

function QueryPostCode(path, data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '8301',
        path: path,
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': Buffer.byteLength(post_data)
        }
    };

    var post_req = http.request(post_options, function (res) {
        res.setEncoding('utf8');
        res.on('data', function (chunk) {
            var json_res = JSON.parse(chunk)
            console.log('Response: ' + chunk);
        })
    });

    post_req.write(post_data);
    post_req.end();
}

function QueryContract(input) {
    var contract_address = fs.readFileSync('contract_address', 'utf-8');
    var data = {
        "input": input,
        'address': contract_address,
        'from': self_account_id,
    };

    QueryPostCode('/query_contract', data);
}

function Prepayment(prepay) {
    var contract_address = fs.readFileSync('contract_address', 'utf-8');
    var data = create_tx(contract_address, 0, 100000, 1, prepay, 7);
    PostCode(data);
}

function Prepayment2(prepay) {
    var contract_address = fs.readFileSync('contract_address', 'utf-8');
    var data = create_tx2(contract_address, 0, 100000, 1, prepay, 7);
    PostCode(data);
}

init_private_key();
const args = process.argv.slice(2)

console.log("args type: " + args[0] + ", size: " + args.length);

// 创建链上数据身份
if (args[0] == 0) {
    var cons_cods = GetConstructorParams(args);
    if (cons_cods == null) {
        console.log("创建数据身份失败，输入的初始管理员错误: " + process.argv);
        return;
    }

    var contract_bytes = "6080604052600080556040516200227b3803806200227b83398181016040528101906200002d919062000361565b60008151905060005b81811015620000ca576001600360008584815181106200005b576200005a620003b2565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff0219169083151502179055508080620000c1906200041a565b91505062000036565b50600080819055506001600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff02191690831515021790555033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550505062000467565b6000604051905090565b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b620001d7826200018c565b810181811067ffffffffffffffff82111715620001f957620001f86200019d565b5b80604052505050565b60006200020e62000173565b90506200021c8282620001cc565b919050565b600067ffffffffffffffff8211156200023f576200023e6200019d565b5b602082029050602081019050919050565b600080fd5b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620002828262000255565b9050919050565b620002948162000275565b8114620002a057600080fd5b50565b600081519050620002b48162000289565b92915050565b6000620002d1620002cb8462000221565b62000202565b90508083825260208201905060208402830185811115620002f757620002f662000250565b5b835b818110156200032457806200030f8882620002a3565b845260208401935050602081019050620002f9565b5050509392505050565b600082601f83011262000346576200034562000187565b5b815162000358848260208601620002ba565b91505092915050565b6000602082840312156200037a57620003796200017d565b5b600082015167ffffffffffffffff8111156200039b576200039a62000182565b5b620003a9848285016200032e565b91505092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b6000819050919050565b6000620004278262000410565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82036200045c576200045b620003e1565b5b600182019050919050565b611e0480620004776000396000f3fe6080604052600436106100a75760003560e01c80638da5cb5b116100645780638da5cb5b1461021c578063e67e3ad914610247578063e802a85014610272578063e8039e351461029b578063e9073fc6146102d8578063f62aef33146102f4576100a7565b80630cdb0d53146100ac578063210d66f8146100e95780632e559c0d14610128578063593b79fe1461016557806370587c10146101a2578063856ed094146101df575b600080fd5b3480156100b857600080fd5b506100d360048036038101906100ce91906112c7565b61031d565b6040516100e0919061138f565b60405180910390f35b3480156100f557600080fd5b50610110600480360381019061010b91906113e7565b610523565b60405161011f93929190611464565b60405180910390f35b34801561013457600080fd5b5061014f600480360381019061014a91906113e7565b6105f5565b60405161015c919061138f565b60405180910390f35b34801561017157600080fd5b5061018c600480360381019061018791906114ce565b610652565b604051610199919061138f565b60405180910390f35b3480156101ae57600080fd5b506101c960048036038101906101c491906115e1565b61067b565b6040516101d6919061138f565b60405180910390f35b3480156101eb57600080fd5b50610206600480360381019061020191906114ce565b610803565b6040516102139190611658565b60405180910390f35b34801561022857600080fd5b50610231610823565b60405161023e9190611673565b60405180910390f35b34801561025357600080fd5b5061025c610849565b604051610269919061138f565b60405180910390f35b34801561027e57600080fd5b5061029960048036038101906102949190611751565b610afa565b005b3480156102a757600080fd5b506102c260048036038101906102bd9190611850565b610bef565b6040516102cf919061138f565b60405180910390f35b6102f260048036038101906102ed91906112c7565b610ef3565b005b34801561030057600080fd5b5061031b60048036038101906103169190611751565b611014565b005b606060006002835161032f91906118db565b67ffffffffffffffff8111156103485761034761119c565b5b6040519080825280601f01601f19166020018201604052801561037a5781602001600182028036833780820191505090505b50905060006040518060400160405280601081526020017f3031323334353637383961626364656600000000000000000000000000000000815250905060005b8451811015610518578182518683815181106103d9576103d861191d565b5b602001015160f81c60f81b60f81c60ff166103f4919061197b565b815181106104055761040461191d565b5b602001015160f81c60f81b8360028361041e91906118db565b8151811061042f5761042e61191d565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a9053508182518683815181106104745761047361191d565b5b602001015160f81c60f81b60f81c60ff1661048f91906119ac565b815181106104a05761049f61191d565b5b602001015160f81c60f81b8360016002846104bb91906118db565b6104c591906119dd565b815181106104d6576104d561191d565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a905350808061051090611a11565b9150506103ba565b508192505050919050565b600260205280600052604060002060009150905080600001805461054690611a88565b80601f016020809104026020016040519081016040528092919081815260200182805461057290611a88565b80156105bf5780601f10610594576101008083540402835291602001916105bf565b820191906000526020600020905b8154815290600101906020018083116105a257829003601f168201915b5050505050908060010160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16908060020154905083565b6060602067ffffffffffffffff8111156106125761061161119c565b5b6040519080825280601f01601f1916602001820160405280156106445781602001600182028036833780820191505090505b509050816020820152919050565b6060816040516020016106659190611b01565b6040516020818303038152906040529050919050565b60606000805b838110156106c55784818151811061069c5761069b61191d565b5b602002602001015151826106b091906119dd565b915080806106bd90611a11565b915050610681565b5060008167ffffffffffffffff8111156106e2576106e161119c565b5b6040519080825280601f01601f1916602001820160405280156107145781602001600182028036833780820191505090505b5090506000805b858110156107f65760005b8782815181106107395761073861191d565b5b6020026020010151518110156107e25787828151811061075c5761075b61191d565b5b602002602001015181815181106107765761077561191d565b5b602001015160f81c60f81b84848061078d90611a11565b9550815181106107a05761079f61191d565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a90535080806107da90611a11565b915050610726565b5080806107ee90611a11565b91505061071b565b5081935050505092915050565b60036020528060005260406000206000915054906101000a900460ff1681565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60606000600260005461085c91906119dd565b67ffffffffffffffff8111156108755761087461119c565b5b6040519080825280602002602001820160405280156108a857816020015b60608152602001906001900390816108935790505b5090506040518060400160405280600181526020017f5b00000000000000000000000000000000000000000000000000000000000000815250816000815181106108f5576108f461191d565b5b602002602001018190525060008054905060006001905060005b82811015610a8757610a3e6002600083815260200190815260200160002060405180606001604052908160008201805461094890611a88565b80601f016020809104026020016040519081016040528092919081815260200182805461097490611a88565b80156109c15780601f10610996576101008083540402835291602001916109c1565b820191906000526020600020905b8154815290600101906020018083116109a457829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001600282015481525050600185610a379190611b1c565b8314610bef565b84600183610a4c91906119dd565b81518110610a5d57610a5c61191d565b5b602002602001018190525081610a7290611a11565b91508080610a7f90611a11565b91505061090f565b506040518060400160405280600181526020017f5d00000000000000000000000000000000000000000000000000000000000000815250838281518110610ad157610ad061191d565b5b6020026020010181905250610af283600183610aed91906119dd565b61067b565b935050505090565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614610b5457600080fd5b60008151905060005b81811015610bea57600160036000858481518110610b7e57610b7d61191d565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff0219169083151502179055508080610be290611a11565b915050610b5d565b505050565b60606000606467ffffffffffffffff811115610c0e57610c0d61119c565b5b604051908082528060200260200182016040528015610c4157816020015b6060815260200190600190039081610c2c5790505b50905060006040518060400160405280600c81526020017f7b2261757468496e666f223a0000000000000000000000000000000000000000815250828280610c8890611a11565b935081518110610c9b57610c9a61191d565b5b60200260200101819052508460000151828280610cb790611a11565b935081518110610cca57610cc961191d565b5b60200260200101819052506040518060400160405280600b81526020017f2c22617574686f72223a22000000000000000000000000000000000000000000815250828280610d1790611a11565b935081518110610d2a57610d2961191d565b5b6020026020010181905250610d4a610d458660200151610652565b61031d565b828280610d5690611a11565b935081518110610d6957610d6861191d565b5b60200260200101819052506040518060400160405280600881526020017f222c226964223a22000000000000000000000000000000000000000000000000815250828280610db690611a11565b935081518110610dc957610dc861191d565b5b6020026020010181905250610de9610de486604001516105f5565b61031d565b828280610df590611a11565b935081518110610e0857610e0761191d565b5b60200260200101819052508315610e7e576040518060400160405280600281526020017f227d000000000000000000000000000000000000000000000000000000000000815250828280610e5b90611a11565b935081518110610e6e57610e6d61191d565b5b6020026020010181905250610edf565b6040518060400160405280600381526020017f227d2c0000000000000000000000000000000000000000000000000000000000815250828280610ec090611a11565b935081518110610ed357610ed261191d565b5b60200260200101819052505b610ee9828261067b565b9250505092915050565b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff16610f4957600080fd5b60405180606001604052808281526020013373ffffffffffffffffffffffffffffffffffffffff16815260200160005481525060026000805481526020019081526020016000206000820151816000019081610fa59190611cfc565b5060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506040820151816002015590505060008081548092919061100c90611a11565b919050555050565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff161461106e57600080fd5b60008151905060005b8181101561116857600360008483815181106110965761109561191d565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff161561115557600360008483815181106111025761110161191d565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81549060ff02191690555b808061116090611a11565b915050611077565b505050565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b6111d48261118b565b810181811067ffffffffffffffff821117156111f3576111f261119c565b5b80604052505050565b600061120661116d565b905061121282826111cb565b919050565b600067ffffffffffffffff8211156112325761123161119c565b5b61123b8261118b565b9050602081019050919050565b82818337600083830152505050565b600061126a61126584611217565b6111fc565b90508281526020810184848401111561128657611285611186565b5b611291848285611248565b509392505050565b600082601f8301126112ae576112ad611181565b5b81356112be848260208601611257565b91505092915050565b6000602082840312156112dd576112dc611177565b5b600082013567ffffffffffffffff8111156112fb576112fa61117c565b5b61130784828501611299565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b8381101561134a57808201518184015260208101905061132f565b60008484015250505050565b600061136182611310565b61136b818561131b565b935061137b81856020860161132c565b6113848161118b565b840191505092915050565b600060208201905081810360008301526113a98184611356565b905092915050565b6000819050919050565b6113c4816113b1565b81146113cf57600080fd5b50565b6000813590506113e1816113bb565b92915050565b6000602082840312156113fd576113fc611177565b5b600061140b848285016113d2565b91505092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b600061143f82611414565b9050919050565b61144f81611434565b82525050565b61145e816113b1565b82525050565b6000606082019050818103600083015261147e8186611356565b905061148d6020830185611446565b61149a6040830184611455565b949350505050565b6114ab81611434565b81146114b657600080fd5b50565b6000813590506114c8816114a2565b92915050565b6000602082840312156114e4576114e3611177565b5b60006114f2848285016114b9565b91505092915050565b600067ffffffffffffffff8211156115165761151561119c565b5b602082029050602081019050919050565b600080fd5b600061153f61153a846114fb565b6111fc565b9050808382526020820190506020840283018581111561156257611561611527565b5b835b818110156115a957803567ffffffffffffffff81111561158757611586611181565b5b8086016115948982611299565b85526020850194505050602081019050611564565b5050509392505050565b600082601f8301126115c8576115c7611181565b5b81356115d884826020860161152c565b91505092915050565b600080604083850312156115f8576115f7611177565b5b600083013567ffffffffffffffff8111156116165761161561117c565b5b611622858286016115b3565b9250506020611633858286016113d2565b9150509250929050565b60008115159050919050565b6116528161163d565b82525050565b600060208201905061166d6000830184611649565b92915050565b60006020820190506116886000830184611446565b92915050565b600067ffffffffffffffff8211156116a9576116a861119c565b5b602082029050602081019050919050565b60006116cd6116c88461168e565b6111fc565b905080838252602082019050602084028301858111156116f0576116ef611527565b5b835b81811015611719578061170588826114b9565b8452602084019350506020810190506116f2565b5050509392505050565b600082601f83011261173857611737611181565b5b81356117488482602086016116ba565b91505092915050565b60006020828403121561176757611766611177565b5b600082013567ffffffffffffffff8111156117855761178461117c565b5b61179184828501611723565b91505092915050565b600080fd5b600080fd5b6000606082840312156117ba576117b961179a565b5b6117c460606111fc565b9050600082013567ffffffffffffffff8111156117e4576117e361179f565b5b6117f084828501611299565b6000830152506020611804848285016114b9565b6020830152506040611818848285016113d2565b60408301525092915050565b61182d8161163d565b811461183857600080fd5b50565b60008135905061184a81611824565b92915050565b6000806040838503121561186757611866611177565b5b600083013567ffffffffffffffff8111156118855761188461117c565b5b611891858286016117a4565b92505060206118a28582860161183b565b9150509250929050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006118e6826113b1565b91506118f1836113b1565b92508282026118ff816113b1565b91508282048414831517611916576119156118ac565b5b5092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b6000611986826113b1565b9150611991836113b1565b9250826119a1576119a061194c565b5b828204905092915050565b60006119b7826113b1565b91506119c2836113b1565b9250826119d2576119d161194c565b5b828206905092915050565b60006119e8826113b1565b91506119f3836113b1565b9250828201905080821115611a0b57611a0a6118ac565b5b92915050565b6000611a1c826113b1565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203611a4e57611a4d6118ac565b5b600182019050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b60006002820490506001821680611aa057607f821691505b602082108103611ab357611ab2611a59565b5b50919050565b60008160601b9050919050565b6000611ad182611ab9565b9050919050565b6000611ae382611ac6565b9050919050565b611afb611af682611434565b611ad8565b82525050565b6000611b0d8284611aea565b60148201915081905092915050565b6000611b27826113b1565b9150611b32836113b1565b9250828203905081811115611b4a57611b496118ac565b5b92915050565b60008190508160005260206000209050919050565b60006020601f8301049050919050565b600082821b905092915050565b600060088302611bb27fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82611b75565b611bbc8683611b75565b95508019841693508086168417925050509392505050565b6000819050919050565b6000611bf9611bf4611bef846113b1565b611bd4565b6113b1565b9050919050565b6000819050919050565b611c1383611bde565b611c27611c1f82611c00565b848454611b82565b825550505050565b600090565b611c3c611c2f565b611c47818484611c0a565b505050565b5b81811015611c6b57611c60600082611c34565b600181019050611c4d565b5050565b601f821115611cb057611c8181611b50565b611c8a84611b65565b81016020851015611c99578190505b611cad611ca585611b65565b830182611c4c565b50505b505050565b600082821c905092915050565b6000611cd360001984600802611cb5565b1980831691505092915050565b6000611cec8383611cc2565b9150826002028217905092915050565b611d0582611310565b67ffffffffffffffff811115611d1e57611d1d61119c565b5b611d288254611a88565b611d33828285611c6f565b600060209050601f831160018114611d665760008415611d54578287015190505b611d5e8582611ce0565b865550611dc6565b601f198416611d7486611b50565b60005b82811015611d9c57848901518255600182019150602085019450602081019050611d77565b86831015611db95784890151611db5601f891682611cc2565b8355505b6001600288020188555050505b50505050505056fea2646970667358221220a8da9cd5fbdc7d749d79d70c4dd9664c5d6702a7cc59f741ebf3b7a6eea050c764736f6c63430008110033";
    new_contract(contract_bytes + cons_cods);
}

// 调用确权预置quota
if (args[0] == 1) {
    Prepayment(100000000000);
   // Prepayment2(100000000000);
}

// 增加数据管理员
if (args[0] == 2) {
    var add_cods = GetAddManagerParams(args);
    if (add_cods == null) {
        console.log("添加管理员失败，输入的初始管理员错误: " + process.argv);
        return;
    }

    call_contract(add_cods, 0);
}

// 删除数据管理员
if (args[0] == 3) {
    var remove_cods = GetRemoveManagerParams(args);
    if (remove_cods == null) {
        console.log("添加管理员失败，输入的初始管理员错误: " + process.argv);
        return;
    }
    
    call_contract(remove_cods, 0);
}

// 确权
if (args[0] == 4) {
    var auth_cods = GetAuthorizationParams(args);
    if (auth_cods == null) {
        console.log("确权失败，输入的确权参数错误: " + process.argv);
        return;
    }
    
    call_contract(auth_cods, 0);
}

// 读取确权列表
if (args[0] == 5) {
    var func = web3.eth.abi.encodeFunctionSignature('GetAuthJson()');
    console.log("GetAuthJson func: " + func.substring(2));
    QueryContract(func.substring(2));
}

if (args[0] == 6) {
    var auth_cods = GetAuthorizationParams(args);
    if (auth_cods == null) {
        console.log("确权失败，输入的确权参数错误: " + process.argv);
        return;
    }

    call_contract2(auth_cods, 0);
}
