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
    const privateKeyBuf = Secp256k1.uint256("9f5acebfa8f32ad4ce046aaa9aaa93406fcc0c91f66543098ac99d64beee7508", 16)
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
    console.log('post_data: ' + post_data);
    console.log('post_data length: ' + Buffer.byteLength(post_data));
    var post_options = {
        host: '127.0.0.1',
        port: '23001',
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

function QueryContract(input) {
    var contract_address = fs.readFileSync('contract_address', 'utf-8');
    var data = {
        "input": input,
        'address': contract_address,
        'from': self_account_id,
    };

    QueryPostCode('/query_contract', data);
}

function QueryPostCode(path, data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '8302',
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
            //var json_res = JSON.parse(chunk)
            //console.log('Response: ' + json_res);
            console.log('Response: ' + chunk);
        })
    });

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

function param_contract(tx_type, gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay) {
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
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, Buffer.from(contract_bytes, 'hex'), Buffer.from(input, 'hex'), prepay_buf]);
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
    var big = ~~(tx_type / MAX_UINT32)
    var low = (tx_type % MAX_UINT32) - big
    step_buf.writeUInt32LE(big, 0)
    step_buf.writeUInt32LE(low, 0)

    var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
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
    var data = param_contract(
        6,
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

function call_contract(input) {
    contract_address = fs.readFileSync('contract_address', 'utf-8');
    console.log("contract_address: " + contract_address);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = param_contract(
        8,
        gid,
        contract_address,
        0,
        10000000,
        1,
        "",
        input,
        0);
    PostCode(data);
}

function do_transaction(to_addr, amount, gas_limit, gas_price) {
    var data = create_tx(to_addr, amount, gas_limit, gas_price);
    PostCode(data);
}

function CreatePhr() {
    console.log("test smart contract signature: ");
    var account1 = web3.eth.accounts.privateKeyToAccount('0x9f5acebfa8f32ad4ce046aaa9aaa93406fcc0c91f66543098ac99d64beee7508');
    console.log("account1 :");
    console.log(account1.address);
    var account2 = web3.eth.accounts.privateKeyToAccount('0x93d851df1e44fa6ba0fbb3731267b967a4e3894e33a8e324a6802c7c06a37628');
    console.log("account2 :");
    console.log(account2.address);
    var account3 = web3.eth.accounts.privateKeyToAccount('0x0665fcd052418c764b9b35b6e4614aaae38457e3d0da369bc47df1794289a2a8');
    console.log("account3 :");
    console.log(account3.address);
    var cons_codes = web3.eth.abi.encodeParameters(['uint256', 'uint256', 'uint256','address'], [1,2,1,account1.address]);
    console.log("cons_codes: " + cons_codes.substring(2));
    
    {
        var updateRootFunc = web3.eth.abi.encodeFunctionSignature('updateRoot(bytes32)');
        var updateRootFunc_param_codes = web3.eth.abi.encodeParameters(['bytes32'], ['0x9f5acebfa8f32ad4ce046aaa9aaa93406fcc0c91f66543098ac99d64beee7508']);
        console.log("updateRootFunc: " + updateRootFunc.substring(2) + updateRootFunc_param_codes.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('GetAuthJson()');
        console.log("GetAuthJson func: " + func.substring(2));
    }

    {
        var GetRemainingStakeAmountFunc = web3.eth.abi.encodeFunctionSignature('GetRemainingStakeAmount(address)');
        var GetRemainingStakeAmountParam = web3.eth.abi.encodeParameters(['address'], [account1.address]);
        console.log("GetRemainingStakeAmount func: " + GetRemainingStakeAmountFunc.substring(2) + GetRemainingStakeAmountParam.substring(2));
    }


    {
        var subscribeDataFunc = web3.eth.abi.encodeFunctionSignature('subscribeData(uint256)');
       var subscribeDataFuncParam = web3.eth.abi.encodeParameters(['uint256'], [1]);
        console.log("subscribeData func: " + subscribeDataFunc.substring(2) + subscribeDataFuncParam.substring(2));
    }

     {
        var rechargeFunc = web3.eth.abi.encodeFunctionSignature('rechargeStake(uint256)');
        var rechargeFuncParam = web3.eth.abi.encodeParameters(['uint256'], [1000]);
        console.log("rechargeStake func: " + rechargeFunc.substring(2) + rechargeFuncParam.substring(2));
    }

    {
        var verifyFunc = web3.eth.abi.encodeFunctionSignature('verifySignatures(address,address,bytes,bytes,uint256,bytes)');
        var test = "test";
        var hashValue = web3.utils.keccak256(test);
        var buyer_sig = web3.eth.accounts.sign(hashValue, '0x93d851df1e44fa6ba0fbb3731267b967a4e3894e33a8e324a6802c7c06a37628');
        var seller_sig = web3.eth.accounts.sign(hashValue, '0x9f5acebfa8f32ad4ce046aaa9aaa93406fcc0c91f66543098ac99d64beee7508');
     
       var verifyFuncParam = web3.eth.abi.encodeParameters(['address','address','bytes','bytes','uint256','bytes'], [account1.address, account2.address, buyer_sig.signature,seller_sig.signature,1,hashValue]);
        console.log("verifySignatures func: " + verifyFunc.substring(2) + verifyFuncParam.substring(2));
        var verifyFuncParam = web3.eth.abi.encodeParameters(['address'], [account1.address]);
        console.log("verifySignatures account1: " + verifyFuncParam.substring(2));
        var verifyFuncParam = web3.eth.abi.encodeParameters(['address'], [account2.address]);
        console.log("verifySignatures account2: " + verifyFuncParam.substring(2));

        var verifyFuncParam = web3.eth.abi.encodeParameters(['bytes'], [buyer_sig.signature]);
        console.log("verifySignatures buyer.sig: " + verifyFuncParam.substring(2));
        var verifyFuncParam = web3.eth.abi.encodeParameters(['bytes'], [seller_sig.signature]);
        console.log("verifySignatures seller.sig: " + verifyFuncParam.substring(2));
        var verifyFuncParam = web3.eth.abi.encodeParameters(['uint256'], [1]);
        console.log("verifySignatures batchNum: " + verifyFuncParam.substring(2));
        var verifyFuncParam = web3.eth.abi.encodeParameters(['bytes'], [hashValue]);
        console.log("verifySignatures hashValue: " + verifyFuncParam.substring(2));
 
}

    {
        var cancelOrderFunc = web3.eth.abi.encodeFunctionSignature('cancelOrder(address)');
        var cancelOrderParam = web3.eth.abi.encodeParameters(['address'], [account2.address]);
        console.log("cancelOrder func: " + cancelOrderFunc.substring(2) + cancelOrderParam.substring(2));
    }

    {
       var distributeRewardsFunc = web3.eth.abi.encodeFunctionSignature('distributeRewards(bool,uint256,address,address)');
       var distributeRewardsParam = web3.eth.abi.encodeParameters(['bool','uint256','address','address'], [false,1,account3.address, account2.address]);
        console.log("distributeRewards func: " + distributeRewardsFunc.substring(2) + distributeRewardsParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('removeListing()');
        console.log("removeListing func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('delistPlagiarist()');
        console.log("delistPlagiarist func: " + func.substring(2));
    }


    {
        var func = web3.eth.abi.encodeFunctionSignature('withdrawsellerReward()');
        console.log("withdrawsellerReward func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('extractsellerReward()');
        console.log("extractsellerReward func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('withdrawOriginatorReward()');
        console.log("withdrawOriginatorReward func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('withdrawBuyerConfirmerReward()');
        console.log("withdrawBuyerConfirmerReward func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('getBatchSignatures(address,uint256)');
        var funcParam = web3.eth.abi.encodeParameters(['address', 'uint256'], [account2.address, 1]);
        console.log("getBatchSignatures func: " + func.substring(2) + funcParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('destroy()');
        console.log("destroy func: " + func.substring(2));
    }

    {
        var recoverFunc = web3.eth.abi.encodeFunctionSignature('Recover()');
        console.log("recoverFunc: " + recoverFunc.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('Transfer(uint64)');
        var funcParam = web3.eth.abi.encodeParameters(['uint64'], [1000]);
        console.log("Transfer func: " + func.substring(2) + funcParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('TransferTo(address,uint64)');
        var funcParam = web3.eth.abi.encodeParameters(['address', 'uint64'], [account1.address, 1000]);
        console.log("TransferTo func: " + func.substring(2) + funcParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('TestQuery()');
        console.log("TestQuery func: " + func.substring(2));
    }


    {
        var topic = web3.utils.keccak256("NewTrade(uint256,address,address,uint256)");
        var func = web3.eth.abi.encodeFunctionSignature('TestEvent(address,uint256)');
        var funcParam = web3.eth.abi.encodeParameters(['address', 'uint256'], [account2.address, 1000]);
        console.log("TestEvent func: " + func.substring(2) + funcParam.substring(2));
        console.log("TestEvent topic: " + topic.substring(2));
    }

    // func code
    {
        var ResAddFunc = web3.eth.abi.encodeFunctionSignature('ResAdd(bytes32,bytes,bytes)');
        var ResAddFunc_param_codes = web3.eth.abi.encodeParameters(['bytes32', 'bytes', 'bytes'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5']);
        console.log("ResAddFunc: " + ResAddFunc.substring(2) + ResAddFunc_param_codes.substring(2));
    }

    {
        var AttrReg = web3.eth.abi.encodeFunctionSignature('AttrReg(bytes,bytes32,bytes[])');
        var test_attr = "test_attr";
        var test_attr_hash = web3.utils.keccak256(test_attr);

        var sig1 = web3.eth.accounts.sign(test_attr_hash, '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
        var sig2 = web3.eth.accounts.sign(test_attr_hash, '0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
        var sig3 = web3.eth.accounts.sign(test_attr_hash, '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
        var AttrReg_param_codes = web3.eth.abi.encodeParameters(['bytes', 'bytes32', 'bytes[]'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', test_attr_hash, [sig1.signature, sig2.signature, sig3.signature]]);
        console.log("AttrReg: " + AttrReg.substring(2) + AttrReg_param_codes.substring(2));
    }

    {
        var UpdateAttr = web3.eth.abi.encodeFunctionSignature('UpdateAttr(bytes,bytes32,bytes[])');
        var test_attr = "test_attr";
        var test_attr_hash = web3.utils.keccak256(test_attr);

        var sig1 = web3.eth.accounts.sign(test_attr_hash, '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
        var sig2 = web3.eth.accounts.sign(test_attr_hash, '0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
        var sig3 = web3.eth.accounts.sign(test_attr_hash, '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
        var UpdateAttr_param_codes = web3.eth.abi.encodeParameters(['bytes', 'bytes32', 'bytes[]'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', test_attr_hash, [sig1.signature, sig2.signature, sig3.signature]]);
        console.log("UpdateAttr: " + UpdateAttr.substring(2) + UpdateAttr_param_codes.substring(2));
    }

    {
        var QuerryAttr = web3.eth.abi.encodeFunctionSignature('QuerryAttr(bytes,bytes32)');
        var test_attr = "test_attr";
        var test_attr_hash = web3.utils.keccak256(test_attr);
        var QuerryAttr_param_codes = web3.eth.abi.encodeParameters(['bytes', 'bytes32'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', test_attr_hash]);
        console.log("QuerryAttr: " + QuerryAttr.substring(2) + QuerryAttr_param_codes.substring(2));
    }

    {
        var PolicyAdd = web3.eth.abi.encodeFunctionSignature('PolicyAdd(bytes32,bytes32,bytes32[],uint256[])');
        var test_attr = "test_attr";
        var test_attr_hash = web3.utils.keccak256(test_attr);
        var PolicyAdd_param_codes = web3.eth.abi.encodeParameters(['bytes32', 'bytes32', 'bytes32[]', 'uint256[]'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', [test_attr_hash], [1784820931]]);
        console.log("PolicyAdd: " + PolicyAdd.substring(2) + PolicyAdd_param_codes.substring(2));
    }

    {
        var PolicyQry = web3.eth.abi.encodeFunctionSignature('PolicyQry(bytes32)');
        var PolicyQry_param_codes = web3.eth.abi.encodeParameters(['bytes32'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5']);
        console.log("PolicyQry: " + PolicyQry.substring(2) + PolicyQry_param_codes.substring(2));
    }

    {
        var Access = web3.eth.abi.encodeFunctionSignature('Access(bytes,bytes32)');
        var Access_param_codes = web3.eth.abi.encodeParameters(['bytes', 'bytes32'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5']);
        console.log("Access: " + Access.substring(2) + Access_param_codes.substring(2));
    }
}

init_private_key();
const args = process.argv.slice(2)
if (args[0] == 0) {
    do_transaction(args[1], 100000, 100000, 1);
}

if (args[0] == 1) {
    new_contract("60806040523480156200001157600080fd5b5060405162005edb38038062005edb8339818101604052810190620000379190620001f1565b600060028462000048919062000292565b146200008b576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401620000829062000351565b60405180910390fd5b336000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555083600481905550826005819055508160068190555080600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600060088190555060006009819055506000600a81905550612710600b819055505050505062000373565b600080fd5b6000819050919050565b620001668162000151565b81146200017257600080fd5b50565b60008151905062000186816200015b565b92915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620001b9826200018c565b9050919050565b620001cb81620001ac565b8114620001d757600080fd5b50565b600081519050620001eb81620001c0565b92915050565b600080600080608085870312156200020e576200020d6200014c565b5b60006200021e8782880162000175565b9450506020620002318782880162000175565b9350506040620002448782880162000175565b92505060606200025787828801620001da565b91505092959194509250565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b60006200029f8262000151565b9150620002ac8362000151565b925082620002bf57620002be62000263565b5b828206905092915050565b600082825260208201905092915050565b7f5f626174636852657175697265645374616b652073686f756c6420626520657660008201527f656e000000000000000000000000000000000000000000000000000000000000602082015250565b600062000339602283620002ca565b91506200034682620002db565b604082019050919050565b600060208201905081810360008301526200036c816200032a565b9050919050565b615b5880620003836000396000f3fe6080604052600436106102255760003560e01c8063601bd9c2116101235780639ae6145b116100ab578063bc44e7391161006f578063bc44e73914610879578063e632f485146108a2578063e67e3ad9146108b9578063eb28c098146108e4578063fad79aea1461090f57610225565b80639ae6145b1461079f5780639e3119fe146107ca5780639f93d34e14610807578063a3e26ce714610830578063b159751f1461084e57610225565b80638b3b5bb3116100f25780638b3b5bb3146106a457806391c4d975146106cf57806397849eb3146106fa57806399c49852146107255780639a9fab401461076257610225565b8063601bd9c2146105fa5780636894e11d1461062557806370587c101461065057806383197ef01461068d57610225565b806331c408d0116101b15780634a07bd2c116101755780634a07bd2c146104ec57806353e0114e146105175780635428263214610554578063556b790f1461057f5780635c71c0df146105bc57610225565b806331c408d014610417578063375b3c0a1461044257806337f37d201461046d5780633d9b2ae61461049857806340143eb5146104c357610225565b80631bff3324116101f85780631bff33241461030c57806321ff9970146103235780632388819c14610360578063294d2bb11461039d5780632e559c0d146103da57610225565b806306136f8d1461022a5780630cdb0d531461026757806313d099f0146102a45780631b466d07146102e1575b600080fd5b34801561023657600080fd5b50610251600480360381019061024c9190614526565b61092d565b60405161025e919061459c565b60405180910390f35b34801561027357600080fd5b5061028e600480360381019061028991906146fd565b610bf0565b60405161029b91906147c5565b60405180910390f35b3480156102b057600080fd5b506102cb60048036038101906102c6919061481d565b610df6565b6040516102d891906147c5565b60405180910390f35b3480156102ed57600080fd5b506102f6610e1f565b6040516103039190614859565b60405180910390f35b34801561031857600080fd5b50610321610e25565b005b34801561032f57600080fd5b5061034a6004803603810190610345919061481d565b610f80565b604051610357919061459c565b60405180910390f35b34801561036c57600080fd5b5061038760048036038101906103829190614874565b611021565b604051610394919061459c565b60405180910390f35b3480156103a957600080fd5b506103c460048036038101906103bf9190614955565b6115af565b6040516103d1919061459c565b60405180910390f35b3480156103e657600080fd5b5061040160048036038101906103fc91906149e0565b611679565b60405161040e91906147c5565b60405180910390f35b34801561042357600080fd5b5061042c6116d6565b604051610439919061459c565b60405180910390f35b34801561044e57600080fd5b50610457611922565b6040516104649190614859565b60405180910390f35b34801561047957600080fd5b50610482611928565b60405161048f919061459c565b60405180910390f35b3480156104a457600080fd5b506104ad611a8a565b6040516104ba9190614a1c565b60405180910390f35b3480156104cf57600080fd5b506104ea60048036038101906104e59190614a37565b611ab0565b005b3480156104f857600080fd5b50610501611bd9565b60405161050e9190614859565b60405180910390f35b34801561052357600080fd5b5061053e60048036038101906105399190614a37565b611bdf565b60405161054b91906147c5565b60405180910390f35b34801561056057600080fd5b50610569611dd7565b6040516105769190614859565b60405180910390f35b34801561058b57600080fd5b506105a660048036038101906105a191906149e0565b611ddd565b6040516105b39190614a1c565b60405180910390f35b3480156105c857600080fd5b506105e360048036038101906105de9190614a37565b611e1c565b6040516105f1929190614adb565b60405180910390f35b34801561060657600080fd5b5061060f611e4d565b60405161061c9190614b13565b60405180910390f35b34801561063157600080fd5b5061063a611e53565b6040516106479190614a1c565b60405180910390f35b34801561065c57600080fd5b5061067760048036038101906106729190614c14565b611e79565b60405161068491906147c5565b60405180910390f35b34801561069957600080fd5b506106a2612001565b005b3480156106b057600080fd5b506106b96124e7565b6040516106c69190614859565b60405180910390f35b3480156106db57600080fd5b506106e46124ed565b6040516106f1919061459c565b60405180910390f35b34801561070657600080fd5b5061070f612b00565b60405161071c9190614a1c565b60405180910390f35b34801561073157600080fd5b5061074c60048036038101906107479190614a37565b612b24565b604051610759919061459c565b60405180910390f35b34801561076e57600080fd5b5061078960048036038101906107849190614c70565b612f93565b6040516107969190614859565b60405180910390f35b3480156107ab57600080fd5b506107b4612fee565b6040516107c1919061459c565b60405180910390f35b3480156107d657600080fd5b506107f160048036038101906107ec9190614c70565b6134a5565b6040516107fe9190614859565b60405180910390f35b34801561081357600080fd5b5061082e60048036038101906108299190614a37565b6134ca565b005b6108386135f3565b604051610845919061459c565b60405180910390f35b34801561085a57600080fd5b5061086361389f565b6040516108709190614a1c565b60405180910390f35b34801561088557600080fd5b506108a0600480360381019061089b9190614a37565b6138c5565b005b3480156108ae57600080fd5b506108b7613996565b005b3480156108c557600080fd5b506108ce613af1565b6040516108db91906147c5565b60405180910390f35b3480156108f057600080fd5b506108f9613c56565b6040516109069190614859565b60405180910390f35b610917613c5c565b604051610924919061459c565b60405180910390f35b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16146109be576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016109b590614d0d565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff168373ffffffffffffffffffffffffffffffffffffffff1603610a2d576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610a2490614d79565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff1603610a9c576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610a9390614de5565b60405180910390fd5b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411610b21576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610b1890614e51565b60405180910390fd5b60005b600654811015610be35760008186610b3c9190614ea0565b90508615610b645760055460086000828254610b589190614ed4565b92505081905550610bc5565b6002600554610b739190614f37565b60096000828254610b849190614ed4565b925050819055506002600554610b9a9190614f37565b600a6000828254610bab9190614ed4565b92505081905550610bbb85611ab0565b610bc4846134ca565b5b610bcf8482613d91565b508080610bdb90614f68565b915050610b24565b5060019050949350505050565b6060600060028351610c029190614fb0565b67ffffffffffffffff811115610c1b57610c1a6145d2565b5b6040519080825280601f01601f191660200182016040528015610c4d5781602001600182028036833780820191505090505b50905060006040518060400160405280601081526020017f3031323334353637383961626364656600000000000000000000000000000000815250905060005b8451811015610deb57818251868381518110610cac57610cab614ff2565b5b602001015160f81c60f81b60f81c60ff16610cc79190614f37565b81518110610cd857610cd7614ff2565b5b602001015160f81c60f81b83600283610cf19190614fb0565b81518110610d0257610d01614ff2565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a905350818251868381518110610d4757610d46614ff2565b5b602001015160f81c60f81b60f81c60ff16610d629190615021565b81518110610d7357610d72614ff2565b5b602001015160f81c60f81b836001600284610d8e9190614fb0565b610d989190614ed4565b81518110610da957610da8614ff2565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a9053508080610de390614f68565b915050610c8d565b508192505050919050565b606081604051602001610e099190615073565b6040516020818303038152906040529050919050565b600a5481565b600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161480610ecc575060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16145b610f0b576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610f0290615126565b60405180910390fd5b6000600a81905550600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc600a549081150290604051600060405180830381858888f19350505050158015610f7d573d6000803e3d6000fd5b50565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614611011576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161100890614d0d565b60405180910390fd5b8160078190555060019050919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16146110b2576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016110a990614d0d565b60405180910390fd5b6000600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411611137576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161112e90614e51565b60405180910390fd5b6000600881111561114b5761114a614a64565b5b600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff1660088111156111ad576111ac614a64565b5b146111ed576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016111e490615192565b60405180910390fd5b600554600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411611273576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161126a90615224565b60405180910390fd5b600554600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411611332576004600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff0219169083600881111561132457611323614a64565b5b0217905550600090506115a5565b61133d8285886115af565b6113b5576003600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff021916908360088111156113a7576113a6614a64565b5b0217905550600090506115a5565b6113c08286896115af565b611438576002600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff0219169083600881111561142a57611429614a64565b5b0217905550600090506115a5565b60006202a300426114499190614ed4565b9050600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020184908060018154018082558091505060019003906000526020600020016000909190919091505580600e60008a73ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600086815260200190815260200160002081905550600554600c60008a73ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001546115599190614ea0565b600c60008a73ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000018190555060019150505b9695505050505050565b600080846040516020016115c391906152d7565b60405160208183030381529060405280519060200120905060008060006115e987614081565b925092509250600184828585604051600081526020016040526040516116129493929190615315565b6020604051602081039080840390855afa158015611634573d6000803e3d6000fd5b5050506020604051035173ffffffffffffffffffffffffffffffffffffffff168673ffffffffffffffffffffffffffffffffffffffff16149450505050509392505050565b6060602067ffffffffffffffff811115611696576116956145d2565b5b6040519080825280601f01601f1916602001820160405280156116c85781602001600182028036833780820191505090505b509050816020820152919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614611767576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161175e90614d0d565b60405180910390fd5b60005b600d80549050811015611914576000600d828154811061178d5761178c614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905060005b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201805490508110156118ff576000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020182815481106118605761185f614ff2565b5b90600052602060002001549050600e60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000828152602001908152602001600020544211156118eb57600554600860008282546118d99190614ed4565b925050819055506118ea8382613d91565b5b5080806118f790614f68565b9150506117bd565b5050808061190c90614f68565b91505061176a565b5061191d611928565b905090565b60045481565b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614806119d1575060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16145b611a10576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401611a07906153cc565b60405180910390fd5b6000600881905550600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6008549081150290604051600060405180830381858888f19350505050158015611a82573d6000803e3d6000fd5b506001905090565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614611b3e576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401611b3590614d0d565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603611bd65780600260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505b50565b60055481565b6060600080600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001541115611c7357600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490505b6000606467ffffffffffffffff811115611c9057611c8f6145d2565b5b604051908082528060200260200182016040528015611cc357816020015b6060815260200190600190039081611cae5790505b50905060006040518060400160405280600c81526020017f5b7b22616d6f756e74223a220000000000000000000000000000000000000000815250828280611d0a90614f68565b935081518110611d1d57611d1c614ff2565b5b6020026020010181905250611d39611d3484611679565b610bf0565b828280611d4590614f68565b935081518110611d5857611d57614ff2565b5b60200260200101819052506040518060400160405280600381526020017f227d5d0000000000000000000000000000000000000000000000000000000000815250828280611da590614f68565b935081518110611db857611db7614ff2565b5b6020026020010181905250611dcd8282611e79565b9350505050919050565b60085481565b600d8181548110611ded57600080fd5b906000526020600020016000915054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600c6020528060005260406000206000915090508060000154908060010160009054906101000a900460ff16905082565b60075481565b600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60606000805b83811015611ec357848181518110611e9a57611e99614ff2565b5b60200260200101515182611eae9190614ed4565b91508080611ebb90614f68565b915050611e7f565b5060008167ffffffffffffffff811115611ee057611edf6145d2565b5b6040519080825280601f01601f191660200182016040528015611f125781602001600182028036833780820191505090505b5090506000805b85811015611ff45760005b878281518110611f3757611f36614ff2565b5b602002602001015151811015611fe057878281518110611f5a57611f59614ff2565b5b60200260200101518181518110611f7457611f73614ff2565b5b602001015160f81c60f81b848480611f8b90614f68565b955081518110611f9e57611f9d614ff2565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a9053508080611fd890614f68565b915050611f24565b508080611fec90614f68565b915050611f19565b5081935050505092915050565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161461208f576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016120869061545e565b60405180910390fd5b60005b600d80549050811015612330576000600d82815481106120b5576120b4614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff169050600560088111156120f6576120f5614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561215857612157614a64565b5b14806121d757506006600881111561217357612172614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff1660088111156121d5576121d4614a64565b5b145b806122555750600760088111156121f1576121f0614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561225357612252614a64565b5b145b612294576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161228b906154ca565b60405180910390fd5b6000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201805490501461231c576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016123139061555c565b60405180910390fd5b50808061232890614f68565b915050612092565b50600060085411156123b057600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6008549081150290604051600060405180830381858888f193505050501580156123a6573d6000803e3d6000fd5b5060006008819055505b6000600954111561242f57600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6009549081150290604051600060405180830381858888f19350505050158015612425573d6000803e3d6000fd5b5060006009819055505b6000600a5411156124ae57600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc600a549081150290604051600060405180830381858888f193505050501580156124a4573d6000803e3d6000fd5b506000600a819055505b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16ff5b60065481565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161461257e576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161257590614d0d565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff161415801561262c5750600073ffffffffffffffffffffffffffffffffffffffff16600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614155b61266b576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612662906155ee565b60405180910390fd5b60005b600d805490508110156128bb576000600d828154811061269157612690614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff169050600560088111156126d2576126d1614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561273457612733614a64565b5b141580156127b657506006600881111561275157612750614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff1660088111156127b3576127b2614a64565b5b14155b80156128365750600760088111156127d1576127d0614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561283357612832614a64565b5b14155b156128a7576006600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff021916908360088111156128a1576128a0614a64565b5b02179055505b5080806128b390614f68565b91505061266e565b5060005b600d805490508110156129fa576000600d82815481106128e2576128e1614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1690506000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490506000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001819055508173ffffffffffffffffffffffffffffffffffffffff166108fc829081150290604051600060405180830381858888f193505050501580156129e4573d6000803e3d6000fd5b50505080806129f290614f68565b9150506128bf565b5060006009541115612a7a576000600981905550600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6009549081150290604051600060405180830381858888f19350505050158015612a78573d6000803e3d6000fd5b505b6000600a541115612af9576000600a81905550600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc600a549081150290604051600060405180830381858888f19350505050158015612af7573d6000803e3d6000fd5b505b6001905090565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614612bb5576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612bac90614d0d565b60405180910390fd5b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411612c3a576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612c3190614e51565b60405180910390fd5b60056008811115612c4e57612c4d614a64565b5b600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612cb057612caf614a64565b5b14158015612d32575060066008811115612ccd57612ccc614a64565b5b600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612d2f57612d2e614a64565b5b14155b8015612db2575060076008811115612d4d57612d4c614a64565b5b600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612daf57612dae614a64565b5b14155b612df1576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612de890615680565b60405180910390fd5b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490506000600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001819055506007600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff02191690836008811115612ee657612ee5614a64565b5b02179055506000600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206002018054905003612f4257612f4183614105565b5b8273ffffffffffffffffffffffffffffffffffffffff166108fc829081150290604051600060405180830381858888f19350505050158015612f88573d6000803e3d6000fd5b506001915050919050565b6000600e60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600083815260200190815260200160002054905092915050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161461307f576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161307690614d0d565b60405180910390fd5b60005b600d8054905081101561349d576000600d82815481106130a5576130a4614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff169050600060088111156130e6576130e5614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561314857613147614a64565b5b14806131c757506001600881111561316357613162614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff1660088111156131c5576131c4614a64565b5b145b806132455750600260088111156131e1576131e0614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561324357613242614a64565b5b145b806132c357506003600881111561325f5761325e614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff1660088111156132c1576132c0614a64565b5b145b806133415750600460088111156132dd576132dc614a64565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561333f5761333e614a64565b5b145b15613489576005600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff021916908360088111156133ac576133ab614a64565b5b02179055506000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490506000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001819055508173ffffffffffffffffffffffffffffffffffffffff166108fc829081150290604051600060405180830381858888f19350505050158015613486573d6000803e3d6000fd5b50505b50808061349590614f68565b915050613082565b506001905090565b600e602052816000526040600020602052806000526040600020600091509150505481565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614613558576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161354f90614d0d565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16036135f05780600360006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505b50565b600060045434101561363a576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161363190615712565b60405180910390fd5b60006002346136499190615021565b14613689576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401613680906157a4565b60405180910390fd5b600b54600d80549050106136d2576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016136c990615836565b60405180910390fd5b6000600c60003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015414613757576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161374e906158a2565b60405180910390fd5b61375f614388565b3481600001818152505060008160200190600881111561378257613781614a64565b5b9081600881111561379657613795614a64565b5b8152505080600c60003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000820151816000015560208201518160010160006101000a81548160ff0219169083600881111561380f5761380e614a64565b5b021790555060408201518160020190805190602001906138309291906143bb565b50905050600d339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600191505090565b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614613953576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161394a90615934565b60405180910390fd5b806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050565b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161480613a3d575060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16145b613a7c576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401613a73906159c6565b60405180910390fd5b6000600981905550600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6009549081150290604051600060405180830381858888f19350505050158015613aee573d6000803e3d6000fd5b50565b60606000606467ffffffffffffffff811115613b1057613b0f6145d2565b5b604051908082528060200260200182016040528015613b4357816020015b6060815260200190600190039081613b2e5790505b50905060006040518060400160405280600f81526020017f5b7b22726f6f7456616c7565223a220000000000000000000000000000000000815250828280613b8a90614f68565b935081518110613b9d57613b9c614ff2565b5b6020026020010181905250613bbb613bb6600754610df6565b610bf0565b828280613bc790614f68565b935081518110613bda57613bd9614ff2565b5b60200260200101819052506040518060400160405280600381526020017f227d5d0000000000000000000000000000000000000000000000000000000000815250828280613c2790614f68565b935081518110613c3a57613c39614ff2565b5b6020026020010181905250613c4f8282611e79565b9250505090565b60095481565b600080600234613c6c9190615021565b14613cac576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401613ca390615a32565b60405180910390fd5b6000600c60003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411613d31576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401613d2890614e51565b60405180910390fd5b34600c60003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000016000828254613d839190614ed4565b925050819055506001905090565b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015411613e16576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401613e0d90614e51565b60405180910390fd5b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201805490509050600081111561407c57600e60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008381526020019081526020016000206000905560005b8181101561407a5782600c60008673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206002018281548110613f1d57613f1c614ff2565b5b90600052602060002001540361406757600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201600183613f7c9190614ea0565b81548110613f8d57613f8c614ff2565b5b9060005260206000200154600c60008673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206002018281548110613fec57613feb614ff2565b5b9060005260206000200181905550600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020180548061404c5761404b615a52565b5b6001900381819060005260206000200160009055905561407a565b808061407290614f68565b915050613ec0565b505b505050565b600080600060418451146140ca576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016140c190615acd565b60405180910390fd5b6020840151925060408401519150606084015160001a9050601b8160ff1610156140fe57601b816140fb9190615aed565b90505b9193909250565b6000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001541161418a576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161418190614e51565b60405180910390fd5b600c60008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000808201600090556001820160006101000a81549060ff02191690556002820160006141f69190614408565b505060005b600d80549050811015614384578173ffffffffffffffffffffffffffffffffffffffff16600d828154811061423357614232614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff160361437157600d6001600d8054905061428d9190614ea0565b8154811061429e5761429d614ff2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16600d82815481106142dd576142dc614ff2565b5b9060005260206000200160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600d80548061433757614336615a52565b5b6001900381819060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff02191690559055614384565b808061437c90614f68565b9150506141fb565b5050565b604051806060016040528060008152602001600060088111156143ae576143ad614a64565b5b8152602001606081525090565b8280548282559060005260206000209081019282156143f7579160200282015b828111156143f65782518255916020019190600101906143db565b5b5090506144049190614429565b5090565b50805460008255906000526020600020908101906144269190614429565b50565b5b8082111561444257600081600090555060010161442a565b5090565b6000604051905090565b600080fd5b600080fd5b60008115159050919050565b61446f8161445a565b811461447a57600080fd5b50565b60008135905061448c81614466565b92915050565b6000819050919050565b6144a581614492565b81146144b057600080fd5b50565b6000813590506144c28161449c565b92915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b60006144f3826144c8565b9050919050565b614503816144e8565b811461450e57600080fd5b50565b600081359050614520816144fa565b92915050565b600080600080608085870312156145405761453f614450565b5b600061454e8782880161447d565b945050602061455f878288016144b3565b935050604061457087828801614511565b925050606061458187828801614511565b91505092959194509250565b6145968161445a565b82525050565b60006020820190506145b1600083018461458d565b92915050565b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b61460a826145c1565b810181811067ffffffffffffffff82111715614629576146286145d2565b5b80604052505050565b600061463c614446565b90506146488282614601565b919050565b600067ffffffffffffffff821115614668576146676145d2565b5b614671826145c1565b9050602081019050919050565b82818337600083830152505050565b60006146a061469b8461464d565b614632565b9050828152602081018484840111156146bc576146bb6145bc565b5b6146c784828561467e565b509392505050565b600082601f8301126146e4576146e36145b7565b5b81356146f484826020860161468d565b91505092915050565b60006020828403121561471357614712614450565b5b600082013567ffffffffffffffff81111561473157614730614455565b5b61473d848285016146cf565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b83811015614780578082015181840152602081019050614765565b60008484015250505050565b600061479782614746565b6147a18185614751565b93506147b1818560208601614762565b6147ba816145c1565b840191505092915050565b600060208201905081810360008301526147df818461478c565b905092915050565b6000819050919050565b6147fa816147e7565b811461480557600080fd5b50565b600081359050614817816147f1565b92915050565b60006020828403121561483357614832614450565b5b600061484184828501614808565b91505092915050565b61485381614492565b82525050565b600060208201905061486e600083018461484a565b92915050565b60008060008060008060c0878903121561489157614890614450565b5b600061489f89828a01614511565b96505060206148b089828a01614511565b955050604087013567ffffffffffffffff8111156148d1576148d0614455565b5b6148dd89828a016146cf565b945050606087013567ffffffffffffffff8111156148fe576148fd614455565b5b61490a89828a016146cf565b935050608061491b89828a016144b3565b92505060a087013567ffffffffffffffff81111561493c5761493b614455565b5b61494889828a016146cf565b9150509295509295509295565b60008060006060848603121561496e5761496d614450565b5b600084013567ffffffffffffffff81111561498c5761498b614455565b5b614998868287016146cf565b935050602084013567ffffffffffffffff8111156149b9576149b8614455565b5b6149c5868287016146cf565b92505060406149d686828701614511565b9150509250925092565b6000602082840312156149f6576149f5614450565b5b6000614a04848285016144b3565b91505092915050565b614a16816144e8565b82525050565b6000602082019050614a316000830184614a0d565b92915050565b600060208284031215614a4d57614a4c614450565b5b6000614a5b84828501614511565b91505092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602160045260246000fd5b60098110614aa457614aa3614a64565b5b50565b6000819050614ab582614a93565b919050565b6000614ac582614aa7565b9050919050565b614ad581614aba565b82525050565b6000604082019050614af0600083018561484a565b614afd6020830184614acc565b9392505050565b614b0d816147e7565b82525050565b6000602082019050614b286000830184614b04565b92915050565b600067ffffffffffffffff821115614b4957614b486145d2565b5b602082029050602081019050919050565b600080fd5b6000614b72614b6d84614b2e565b614632565b90508083825260208201905060208402830185811115614b9557614b94614b5a565b5b835b81811015614bdc57803567ffffffffffffffff811115614bba57614bb96145b7565b5b808601614bc789826146cf565b85526020850194505050602081019050614b97565b5050509392505050565b600082601f830112614bfb57614bfa6145b7565b5b8135614c0b848260208601614b5f565b91505092915050565b60008060408385031215614c2b57614c2a614450565b5b600083013567ffffffffffffffff811115614c4957614c48614455565b5b614c5585828601614be6565b9250506020614c66858286016144b3565b9150509250929050565b60008060408385031215614c8757614c86614450565b5b6000614c9585828601614511565b9250506020614ca6858286016144b3565b9150509250929050565b600082825260208201905092915050565b7f4f6e6c7920706c6174666f726d2063616e2063616c6c20746869732e00000000600082015250565b6000614cf7601c83614cb0565b9150614d0282614cc1565b602082019050919050565b60006020820190508181036000830152614d2681614cea565b9050919050565b7f496e76616c6964206f726967696e61746f7220616464726573732e0000000000600082015250565b6000614d63601b83614cb0565b9150614d6e82614d2d565b602082019050919050565b60006020820190508181036000830152614d9281614d56565b9050919050565b7f496e76616c696420627579657220636f6e6669726d657220616464726573732e600082015250565b6000614dcf602083614cb0565b9150614dda82614d99565b602082019050919050565b60006020820190508181036000830152614dfe81614dc2565b9050919050565b7f546865206275796572206e6f742065786973742e000000000000000000000000600082015250565b6000614e3b601483614cb0565b9150614e4682614e05565b602082019050919050565b60006020820190508181036000830152614e6a81614e2e565b9050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b6000614eab82614492565b9150614eb683614492565b9250828203905081811115614ece57614ecd614e71565b5b92915050565b6000614edf82614492565b9150614eea83614492565b9250828201905080821115614f0257614f01614e71565b5b92915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b6000614f4282614492565b9150614f4d83614492565b925082614f5d57614f5c614f08565b5b828204905092915050565b6000614f7382614492565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203614fa557614fa4614e71565b5b600182019050919050565b6000614fbb82614492565b9150614fc683614492565b9250828202614fd481614492565b91508282048414831517614feb57614fea614e71565b5b5092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b600061502c82614492565b915061503783614492565b92508261504757615046614f08565b5b828206905092915050565b6000819050919050565b61506d615068826147e7565b615052565b82525050565b600061507f828461505c565b60208201915081905092915050565b7f4f6e6c792074686520627579657220636f6e6669726d65722063616e2077697460008201527f68647261772074686520627579657220636f6e6669726d65722072657761726460208201527f2e00000000000000000000000000000000000000000000000000000000000000604082015250565b6000615110604183614cb0565b915061511b8261508e565b606082019050919050565b6000602082019050818103600083015261513f81615103565b9050919050565b7f546865206f72646572206973206e6f7420696e207573652e0000000000000000600082015250565b600061517c601883614cb0565b915061518782615146565b602082019050919050565b600060208201905081810360008301526151ab8161516f565b9050919050565b7f5468652072656d61696e696e67207374616b65206973206e6f7420656e6f756760008201527f6820666f7220612062617463682e000000000000000000000000000000000000602082015250565b600061520e602e83614cb0565b9150615219826151b2565b604082019050919050565b6000602082019050818103600083015261523d81615201565b9050919050565b600081905092915050565b7f19457468657265756d205369676e6564204d6573736167653a0a333200000000600082015250565b6000615285601c83615244565b91506152908261524f565b601c82019050919050565b600081905092915050565b60006152b182614746565b6152bb818561529b565b93506152cb818560208601614762565b80840191505092915050565b60006152e282615278565b91506152ee82846152a6565b915081905092915050565b600060ff82169050919050565b61530f816152f9565b82525050565b600060808201905061532a6000830187614b04565b6153376020830186615306565b6153446040830185614b04565b6153516060830184614b04565b95945050505050565b7f4f6e6c79207468652073656c6c65722063616e2077697468647261772074686560008201527f206f776e6572207265776172642e000000000000000000000000000000000000602082015250565b60006153b6602e83614cb0565b91506153c18261535a565b604082019050919050565b600060208201905081810360008301526153e5816153a9565b9050919050565b7f4f6e6c7920706c6174666f726d206163636f756e742063616e2064657374726f60008201527f792074686520636f6e74726163742e0000000000000000000000000000000000602082015250565b6000615448602f83614cb0565b9150615453826153ec565b604082019050919050565b600060208201905081810360008301526154778161543b565b9050919050565b7f546865726520617265207374696c6c20616374697665206f72646572732e0000600082015250565b60006154b4601e83614cb0565b91506154bf8261547e565b602082019050919050565b600060208201905081810360008301526154e3816154a7565b9050919050565b7f546865726520617265207374696c6c20756e6578747261637465642070726f6660008201527f6974732e00000000000000000000000000000000000000000000000000000000602082015250565b6000615546602483614cb0565b9150615551826154ea565b604082019050919050565b6000602082019050818103600083015261557581615539565b9050919050565b7f52696768747320636f6e6669726d6174696f6e20686173206e6f74206265656e60008201527f20636f6d706c657465642e000000000000000000000000000000000000000000602082015250565b60006155d8602b83614cb0565b91506155e38261557c565b604082019050919050565b60006020820190508181036000830152615607816155cb565b9050919050565b7f4f7264657220697320696e20612066696e616c20737461746520616e6420636160008201527f6e6e6f742062652063616e63656c6c65642e0000000000000000000000000000602082015250565b600061566a603283614cb0565b91506156758261560e565b604082019050919050565b600060208201905081810360008301526156998161565d565b9050919050565b7f546865207374616b6520616d6f756e74206973206c657373207468616e20746860008201527f65206d696e696d756d207265717569726564207374616b652e00000000000000602082015250565b60006156fc603983614cb0565b9150615707826156a0565b604082019050919050565b6000602082019050818103600083015261572b816156ef565b9050919050565b7f546865207374616b6520616d6f756e74206e656564206576656e206e756d626560008201527f722e000000000000000000000000000000000000000000000000000000000000602082015250565b600061578e602283614cb0565b915061579982615732565b604082019050919050565b600060208201905081810360008301526157bd81615781565b9050919050565b7f54686520627579657220636f756e742073686f756c64206c657373207468616e60008201527f20746865206d617853697a655468726573686f6c642e00000000000000000000602082015250565b6000615820603683614cb0565b915061582b826157c4565b604082019050919050565b6000602082019050818103600083015261584f81615813565b9050919050565b7f546865206275796572206861732065786973742e000000000000000000000000600082015250565b600061588c601483614cb0565b915061589782615856565b602082019050919050565b600060208201905081810360008301526158bb8161587f565b9050919050565b7f4f6e6c79206578697374696e6720706c6174666f726d206163636f756e74206360008201527f616e206368616e67652074686520706c6174666f726d206163636f756e742e00602082015250565b600061591e603f83614cb0565b9150615929826158c2565b604082019050919050565b6000602082019050818103600083015261594d81615911565b9050919050565b7f4f6e6c7920746865206f726967696e61746f722063616e20776974686472617760008201527f20746865206f726967696e61746f72207265776172642e000000000000000000602082015250565b60006159b0603783614cb0565b91506159bb82615954565b604082019050919050565b600060208201905081810360008301526159df816159a3565b9050919050565b7f5468652073656e742076616c7565206e656564206576656e206e756d6265722e600082015250565b6000615a1c602083614cb0565b9150615a27826159e6565b602082019050919050565b60006020820190508181036000830152615a4b81615a0f565b9050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603160045260246000fd5b7f496e76616c6964207369676e6174757265206c656e6774680000000000000000600082015250565b6000615ab7601883614cb0565b9150615ac282615a81565b602082019050919050565b60006020820190508181036000830152615ae681615aaa565b9050919050565b6000615af8826152f9565b9150615b03836152f9565b9250828201905060ff811115615b1c57615b1b614e71565b5b9291505056fea26469706673582212200537543c8bbb28a62601f095756a4d3ce5ab63090ced9cd80b016bf552d7276064736f6c6343000811003300000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000100000000000000000000000027d4c39244f26c157b5a87898569ef4ce5807413");
}

if (args[0] == 2) {
    call_contract(args[1]);
}

if (args[0] == 3) {
    CreatePhr();
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}


// 读取
 if (args[0] == 5) {
     var func = web3.eth.abi.encodeFunctionSignature('GetAuthJson()');
     console.log("GetAuthJson func: " + func.substring(2));
     sleep(5000);
     QueryContract(func.substring(2));
  }


if (args[0] == 6) {
   Prepayment2(100000000000);
}


function Prepayment2( prepay) {
     var contract_address = fs.readFileSync('contract_address', 'utf-8');
     var data = create_tx3(contract_address, 0, 100000, 1, prepay, 7);
     PostCode2(data);
}
     

function create_tx3(to, amount, gas_limit, gas_price, prepay, tx_type) {
    const privateKeyBuf2 = Secp256k1.uint256("9f5acebfa8f32ad4ce046aaa9aaa93406fcc0c91f66543098ac99d64beee7508", 16)
    self_private_key2 = Secp256k1.uint256(privateKeyBuf2, 16)
    self_public_key2 = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key2)

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
        'shard_id': 3,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
        'pepay': prepay
    }
}
     

function PostCode2(data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '8302',
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

    post_req.write(post_data);
    post_req.end();
}
    
