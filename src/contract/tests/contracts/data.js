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
    const privateKeyBuf = Secp256k1.uint256("7089ec759881d637501df388b6814c45579d643e36b90df36e59576b0648682c", 16)
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
    var account1 = web3.eth.accounts.privateKeyToAccount('0x02b91d27bb1761688be87898c44772e727f5e2f64aaf51a42931a0ca66a8a227');
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
        var updateRootFunc_param_codes = web3.eth.abi.encodeParameters(['bytes32'], ['0x02b91d27bb1761688be87898c44772e727f5e2f64aaf51a42931a0ca66a8a227']);
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
        var subscribeDataFuncParam = web3.eth.abi.encodeParameters(['uint256'], [1000]);
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
        var seller_sig = web3.eth.accounts.sign(hashValue, '0x02b91d27bb1761688be87898c44772e727f5e2f64aaf51a42931a0ca66a8a227');
     
       var verifyFuncParam = web3.eth.abi.encodeParameters(['address','address','bytes','bytes','uint256','bytes'], [account1.address, account2.address, buyer_sig.signature,seller_sig.signature,1,hashValue]);
        console.log("verifySignatures func: " + verifyFunc.substring(2) + verifyFuncParam.substring(2));
    }

    {
        var cancelOrderFunc = web3.eth.abi.encodeFunctionSignature('cancelOrder(address)');
        var cancelOrderParam = web3.eth.abi.encodeParameters(['address'], [account2.address]);
        console.log("cancelOrder func: " + cancelOrderFunc.substring(2) + cancelOrderParam.substring(2));
    }

    {
       var distributeRewardsFunc = web3.eth.abi.encodeFunctionSignature('distributeRewards(bool,uint256,address,address)');
       var distributeRewardsParam = web3.eth.abi.encodeParameters(['bool','uint256','address','address'], [false,1,account1.address, account2.address]);
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
    new_contract("60806040523480156200001157600080fd5b506040516200540a3803806200540a8339818101604052810190620000379190620001f1565b600060028462000048919062000292565b146200008b576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401620000829062000351565b60405180910390fd5b336000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555083600481905550826005819055508160068190555080600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600060088190555060006009819055506000600a81905550612710600b819055505050505062000373565b600080fd5b6000819050919050565b620001668162000151565b81146200017257600080fd5b50565b60008151905062000186816200015b565b92915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620001b9826200018c565b9050919050565b620001cb81620001ac565b8114620001d757600080fd5b50565b600081519050620001eb81620001c0565b92915050565b600080600080608085870312156200020e576200020d6200014c565b5b60006200021e8782880162000175565b9450506020620002318782880162000175565b9350506040620002448782880162000175565b92505060606200025787828801620001da565b91505092959194509250565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b60006200029f8262000151565b9150620002ac8362000151565b925082620002bf57620002be62000263565b5b828206905092915050565b600082825260208201905092915050565b7f5f626174636852657175697265645374616b652073686f756c6420626520657660008201527f656e000000000000000000000000000000000000000000000000000000000000602082015250565b600062000339602283620002ca565b91506200034682620002db565b604082019050919050565b600060208201905081810360008301526200036c816200032a565b9050919050565b61508780620003836000396000f3fe6080604052600436106102045760003560e01c80636894e11d116101185780639e3119fe116100a0578063ba6eaffc1161006f578063ba6eaffc146107b3578063bc44e739146107e3578063e632f4851461080c578063e67e3ad914610823578063eb28c0981461084e57610204565b80639e3119fe146106f25780639f93d34e1461072f578063a11c35af14610758578063b159751f1461078857610204565b806391c4d975116100e757806391c4d975146105f757806397849eb31461062257806399c498521461064d5780639a9fab401461068a5780639ae6145b146106c757610204565b80636894e11d1461054d57806370587c101461057857806383197ef0146105b55780638b3b5bb3146105cc57610204565b8063375b3c0a1161019b5780634a07bd2c1161016a5780634a07bd2c14610451578063542826321461047c578063556b790f146104a75780635c71c0df146104e4578063601bd9c21461052257610204565b8063375b3c0a146103a757806337f37d20146103d25780633d9b2ae6146103fd57806340143eb51461042857610204565b806321ff9970116101d757806321ff9970146102c55780632388819c14610302578063294d2bb11461033f57806331c408d01461037c57610204565b806306136f8d1461020957806313d099f0146102465780631b466d07146102835780631bff3324146102ae575b600080fd5b34801561021557600080fd5b50610230600480360381019061022b9190613bc3565b610879565b60405161023d9190613c39565b60405180910390f35b34801561025257600080fd5b5061026d60048036038101906102689190613c8a565b610ab7565b60405161027a9190613d47565b60405180910390f35b34801561028f57600080fd5b50610298610ae0565b6040516102a59190613d78565b60405180910390f35b3480156102ba57600080fd5b506102c3610ae6565b005b3480156102d157600080fd5b506102ec60048036038101906102e79190613c8a565b610c41565b6040516102f99190613c39565b60405180910390f35b34801561030e57600080fd5b5061032960048036038101906103249190613ec8565b610ce2565b6040516103369190613c39565b60405180910390f35b34801561034b57600080fd5b5061036660048036038101906103619190613fa9565b611157565b6040516103739190613c39565b60405180910390f35b34801561038857600080fd5b50610391611221565b60405161039e9190613c39565b60405180910390f35b3480156103b357600080fd5b506103bc61146d565b6040516103c99190613d78565b60405180910390f35b3480156103de57600080fd5b506103e7611473565b6040516103f49190613c39565b60405180910390f35b34801561040957600080fd5b506104126115d5565b60405161041f9190614043565b60405180910390f35b34801561043457600080fd5b5061044f600480360381019061044a919061405e565b6115fb565b005b34801561045d57600080fd5b50610466611724565b6040516104739190613d78565b60405180910390f35b34801561048857600080fd5b5061049161172a565b60405161049e9190613d78565b60405180910390f35b3480156104b357600080fd5b506104ce60048036038101906104c9919061408b565b611730565b6040516104db9190614043565b60405180910390f35b3480156104f057600080fd5b5061050b6004803603810190610506919061405e565b61176f565b60405161051992919061412f565b60405180910390f35b34801561052e57600080fd5b506105376117a0565b6040516105449190614167565b60405180910390f35b34801561055957600080fd5b506105626117a6565b60405161056f9190614043565b60405180910390f35b34801561058457600080fd5b5061059f600480360381019061059a9190614268565b6117cc565b6040516105ac9190613d47565b60405180910390f35b3480156105c157600080fd5b506105ca611954565b005b3480156105d857600080fd5b506105e1611e3a565b6040516105ee9190613d78565b60405180910390f35b34801561060357600080fd5b5061060c611e40565b6040516106199190613c39565b60405180910390f35b34801561062e57600080fd5b50610637612453565b6040516106449190614043565b60405180910390f35b34801561065957600080fd5b50610674600480360381019061066f919061405e565b612477565b6040516106819190613c39565b60405180910390f35b34801561069657600080fd5b506106b160048036038101906106ac91906142c4565b612861565b6040516106be9190613d78565b60405180910390f35b3480156106d357600080fd5b506106dc6128bc565b6040516106e99190613c39565b60405180910390f35b3480156106fe57600080fd5b50610719600480360381019061071491906142c4565b612d73565b6040516107269190613d78565b60405180910390f35b34801561073b57600080fd5b506107566004803603810190610751919061405e565b612d98565b005b610772600480360381019061076d919061408b565b612ec1565b60405161077f9190613c39565b60405180910390f35b34801561079457600080fd5b5061079d612f67565b6040516107aa9190614043565b60405180910390f35b6107cd60048036038101906107c8919061408b565b612f8d565b6040516107da9190613c39565b60405180910390f35b3480156107ef57600080fd5b5061080a6004803603810190610805919061405e565b6131a9565b005b34801561081857600080fd5b5061082161327a565b005b34801561082f57600080fd5b506108386133d5565b6040516108459190613d47565b60405180910390f35b34801561085a57600080fd5b50610863613532565b6040516108709190613d78565b60405180910390f35b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161461090a576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161090190614361565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff168373ffffffffffffffffffffffffffffffffffffffff1603610979576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610970906143cd565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff16036109e8576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016109df90614439565b60405180910390fd5b60005b600654811015610aaa5760008186610a039190614488565b90508615610a2b5760055460086000828254610a1f91906144bc565b92505081905550610a8c565b6002600554610a3a919061451f565b60096000828254610a4b91906144bc565b925050819055506002600554610a61919061451f565b600a6000828254610a7291906144bc565b92505081905550610a82856115fb565b610a8b84612d98565b5b610a968482613538565b508080610aa290614550565b9150506109eb565b5060019050949350505050565b606081604051602001610aca91906145b9565b6040516020818303038152906040529050919050565b600a5481565b600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161480610b8d575060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16145b610bcc576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610bc39061466c565b60405180910390fd5b6000600a81905550600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc600a549081150290604051600060405180830381858888f19350505050158015610c3e573d6000803e3d6000fd5b50565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614610cd2576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610cc990614361565b60405180910390fd5b8160078190555060019050919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614610d73576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610d6a90614361565b60405180910390fd5b60006008811115610d8757610d866140b8565b5b600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115610de957610de86140b8565b5b14610e29576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610e20906146d8565b60405180910390fd5b600554600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001541015610eb0576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401610ea79061476a565b60405180910390fd5b600554600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001541015610f70576004600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff02191690836008811115610f6257610f616140b8565b5b02179055506000905061114d565b610f7b828588611157565b610ff3576003600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff02191690836008811115610fe557610fe46140b8565b5b02179055506000905061114d565b610ffe828689611157565b611076576002600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff02191690836008811115611068576110676140b8565b5b02179055506000905061114d565b60006202a3004261108791906144bc565b9050600c60008973ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020184908060018154018082558091505060019003906000526020600020016000909190919091505580600e60008a73ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008681526020019081526020016000208190555060019150505b9695505050505050565b6000808460405160200161116b919061481d565b6040516020818303038152906040528051906020012090506000806000611191876137a3565b925092509250600184828585604051600081526020016040526040516111ba949392919061485b565b6020604051602081039080840390855afa1580156111dc573d6000803e3d6000fd5b5050506020604051035173ffffffffffffffffffffffffffffffffffffffff168673ffffffffffffffffffffffffffffffffffffffff16149450505050509392505050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16146112b2576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016112a990614361565b60405180910390fd5b60005b600d8054905081101561145f576000600d82815481106112d8576112d76148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905060005b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206002018054905081101561144a576000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020182815481106113ab576113aa6148a0565b5b90600052602060002001549050600e60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600082815260200190815260200160002054421115611436576005546008600082825461142491906144bc565b925050819055506114358382613538565b5b50808061144290614550565b915050611308565b5050808061145790614550565b9150506112b5565b50611468611473565b905090565b60045481565b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16148061151c575060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16145b61155b576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161155290614941565b60405180910390fd5b6000600881905550600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6008549081150290604051600060405180830381858888f193505050501580156115cd573d6000803e3d6000fd5b506001905090565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614611689576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161168090614361565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16036117215780600260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505b50565b60055481565b60085481565b600d818154811061174057600080fd5b906000526020600020016000915054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600c6020528060005260406000206000915090508060000154908060010160009054906101000a900460ff16905082565b60075481565b600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60606000805b83811015611816578481815181106117ed576117ec6148a0565b5b6020026020010151518261180191906144bc565b9150808061180e90614550565b9150506117d2565b5060008167ffffffffffffffff81111561183357611832613d9d565b5b6040519080825280601f01601f1916602001820160405280156118655781602001600182028036833780820191505090505b5090506000805b858110156119475760005b87828151811061188a576118896148a0565b5b602002602001015151811015611933578782815181106118ad576118ac6148a0565b5b602002602001015181815181106118c7576118c66148a0565b5b602001015160f81c60f81b8484806118de90614550565b9550815181106118f1576118f06148a0565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a905350808061192b90614550565b915050611877565b50808061193f90614550565b91505061186c565b5081935050505092915050565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16146119e2576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016119d9906149d3565b60405180910390fd5b60005b600d80549050811015611c83576000600d8281548110611a0857611a076148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905060056008811115611a4957611a486140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115611aab57611aaa6140b8565b5b1480611b2a575060066008811115611ac657611ac56140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115611b2857611b276140b8565b5b145b80611ba8575060076008811115611b4457611b436140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115611ba657611ba56140b8565b5b145b611be7576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401611bde90614a3f565b60405180910390fd5b6000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206002018054905014611c6f576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401611c6690614ad1565b60405180910390fd5b508080611c7b90614550565b9150506119e5565b5060006008541115611d0357600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6008549081150290604051600060405180830381858888f19350505050158015611cf9573d6000803e3d6000fd5b5060006008819055505b60006009541115611d8257600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6009549081150290604051600060405180830381858888f19350505050158015611d78573d6000803e3d6000fd5b5060006009819055505b6000600a541115611e0157600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc600a549081150290604051600060405180830381858888f19350505050158015611df7573d6000803e3d6000fd5b506000600a819055505b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16ff5b60065481565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614611ed1576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401611ec890614361565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614158015611f7f5750600073ffffffffffffffffffffffffffffffffffffffff16600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614155b611fbe576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401611fb590614b63565b60405180910390fd5b60005b600d8054905081101561220e576000600d8281548110611fe457611fe36148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905060056008811115612025576120246140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612087576120866140b8565b5b141580156121095750600660088111156120a4576120a36140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612106576121056140b8565b5b14155b8015612189575060076008811115612124576121236140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612186576121856140b8565b5b14155b156121fa576006600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff021916908360088111156121f4576121f36140b8565b5b02179055505b50808061220690614550565b915050611fc1565b5060005b600d8054905081101561234d576000600d8281548110612235576122346148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1690506000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490506000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001819055508173ffffffffffffffffffffffffffffffffffffffff166108fc829081150290604051600060405180830381858888f19350505050158015612337573d6000803e3d6000fd5b505050808061234590614550565b915050612212565b50600060095411156123cd576000600981905550600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6009549081150290604051600060405180830381858888f193505050501580156123cb573d6000803e3d6000fd5b505b6000600a54111561244c576000600a81905550600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc600a549081150290604051600060405180830381858888f1935050505015801561244a573d6000803e3d6000fd5b505b6001905090565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614612508576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016124ff90614361565b60405180910390fd5b6005600881111561251c5761251b6140b8565b5b600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561257e5761257d6140b8565b5b1415801561260057506006600881111561259b5761259a6140b8565b5b600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff1660088111156125fd576125fc6140b8565b5b14155b801561268057506007600881111561261b5761261a6140b8565b5b600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff16600881111561267d5761267c6140b8565b5b14155b6126bf576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016126b690614bf5565b60405180910390fd5b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490506000600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001819055506007600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff021916908360088111156127b4576127b36140b8565b5b02179055506000600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020180549050036128105761280f83613827565b5b8273ffffffffffffffffffffffffffffffffffffffff166108fc829081150290604051600060405180830381858888f19350505050158015612856573d6000803e3d6000fd5b506001915050919050565b6000600e60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600083815260200190815260200160002054905092915050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161461294d576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161294490614361565b60405180910390fd5b60005b600d80549050811015612d6b576000600d8281548110612973576129726148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff169050600060088111156129b4576129b36140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612a1657612a156140b8565b5b1480612a95575060016008811115612a3157612a306140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612a9357612a926140b8565b5b145b80612b13575060026008811115612aaf57612aae6140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612b1157612b106140b8565b5b145b80612b91575060036008811115612b2d57612b2c6140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612b8f57612b8e6140b8565b5b145b80612c0f575060046008811115612bab57612baa6140b8565b5b600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160009054906101000a900460ff166008811115612c0d57612c0c6140b8565b5b145b15612d57576005600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060010160006101000a81548160ff02191690836008811115612c7a57612c796140b8565b5b02179055506000600c60008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000015490506000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600001819055508173ffffffffffffffffffffffffffffffffffffffff166108fc829081150290604051600060405180830381858888f19350505050158015612d54573d6000803e3d6000fd5b50505b508080612d6390614550565b915050612950565b506001905090565b600e602052816000526040600020602052806000526040600020600091509150505481565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614612e26576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612e1d90614361565b60405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603612ebe5780600360006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505b50565b6000813414612f05576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612efc90614c87565b60405180910390fd5b81600c60003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000016000828254612f5791906144bc565b9250508190555060019050919050565b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b6000813414612fd1576040517f08c379a0000000000000000000000000000000000000000000000000000000008152600401612fc890614d19565b60405180910390fd5b600454821015613016576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161300d90614dab565b60405180910390fd5b600b54600d805490501061305f576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161305690614e3d565b60405180910390fd5b613067613a25565b8281600001818152505060008160200190600881111561308a576130896140b8565b5b9081600881111561309e5761309d6140b8565b5b8152505080600c60003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000820151816000015560208201518160010160006101000a81548160ff02191690836008811115613117576131166140b8565b5b02179055506040820151816002019080519060200190613138929190613a58565b50905050600d339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506001915050919050565b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614613237576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161322e90614ecf565b60405180910390fd5b806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050565b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161480613321575060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff16145b613360576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040161335790614f61565b60405180910390fd5b6000600981905550600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6009549081150290604051600060405180830381858888f193505050501580156133d2573d6000803e3d6000fd5b50565b60606000606467ffffffffffffffff8111156133f4576133f3613d9d565b5b60405190808252806020026020018201604052801561342757816020015b60608152602001906001900390816134125790505b50905060006040518060400160405280600e81526020017f5b7b22726f6f7456616c7565223a00000000000000000000000000000000000081525082828061346e90614550565b935081518110613481576134806148a0565b5b6020026020010181905250613497600754610ab7565b8282806134a390614550565b9350815181106134b6576134b56148a0565b5b60200260200101819052506040518060400160405280600381526020017f227d5d000000000000000000000000000000000000000000000000000000000081525082828061350390614550565b935081518110613516576135156148a0565b5b602002602001018190525061352b82826117cc565b9250505090565b60095481565b6000600c60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201805490509050600081111561379e57600e60008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008381526020019081526020016000206000905560005b8181101561379c5782600c60008673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201828154811061363f5761363e6148a0565b5b90600052602060002001540361378957600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020160018361369e9190614488565b815481106136af576136ae6148a0565b5b9060005260206000200154600c60008673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201828154811061370e5761370d6148a0565b5b9060005260206000200181905550600c60008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020180548061376e5761376d614f81565b5b6001900381819060005260206000200160009055905561379c565b808061379490614550565b9150506135e2565b505b505050565b600080600060418451146137ec576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016137e390614ffc565b60405180910390fd5b6020840151925060408401519150606084015160001a9050601b8160ff16101561382057601b8161381d919061501c565b90505b9193909250565b600c60008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000808201600090556001820160006101000a81549060ff02191690556002820160006138939190613aa5565b505060005b600d80549050811015613a21578173ffffffffffffffffffffffffffffffffffffffff16600d82815481106138d0576138cf6148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603613a0e57600d6001600d8054905061392a9190614488565b8154811061393b5761393a6148a0565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16600d828154811061397a576139796148a0565b5b9060005260206000200160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600d8054806139d4576139d3614f81565b5b6001900381819060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff02191690559055613a21565b8080613a1990614550565b915050613898565b5050565b60405180606001604052806000815260200160006008811115613a4b57613a4a6140b8565b5b8152602001606081525090565b828054828255906000526020600020908101928215613a94579160200282015b82811115613a93578251825591602001919060010190613a78565b5b509050613aa19190613ac6565b5090565b5080546000825590600052602060002090810190613ac39190613ac6565b50565b5b80821115613adf576000816000905550600101613ac7565b5090565b6000604051905090565b600080fd5b600080fd5b60008115159050919050565b613b0c81613af7565b8114613b1757600080fd5b50565b600081359050613b2981613b03565b92915050565b6000819050919050565b613b4281613b2f565b8114613b4d57600080fd5b50565b600081359050613b5f81613b39565b92915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000613b9082613b65565b9050919050565b613ba081613b85565b8114613bab57600080fd5b50565b600081359050613bbd81613b97565b92915050565b60008060008060808587031215613bdd57613bdc613aed565b5b6000613beb87828801613b1a565b9450506020613bfc87828801613b50565b9350506040613c0d87828801613bae565b9250506060613c1e87828801613bae565b91505092959194509250565b613c3381613af7565b82525050565b6000602082019050613c4e6000830184613c2a565b92915050565b6000819050919050565b613c6781613c54565b8114613c7257600080fd5b50565b600081359050613c8481613c5e565b92915050565b600060208284031215613ca057613c9f613aed565b5b6000613cae84828501613c75565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b83811015613cf1578082015181840152602081019050613cd6565b60008484015250505050565b6000601f19601f8301169050919050565b6000613d1982613cb7565b613d238185613cc2565b9350613d33818560208601613cd3565b613d3c81613cfd565b840191505092915050565b60006020820190508181036000830152613d618184613d0e565b905092915050565b613d7281613b2f565b82525050565b6000602082019050613d8d6000830184613d69565b92915050565b600080fd5b600080fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b613dd582613cfd565b810181811067ffffffffffffffff82111715613df457613df3613d9d565b5b80604052505050565b6000613e07613ae3565b9050613e138282613dcc565b919050565b600067ffffffffffffffff821115613e3357613e32613d9d565b5b613e3c82613cfd565b9050602081019050919050565b82818337600083830152505050565b6000613e6b613e6684613e18565b613dfd565b905082815260208101848484011115613e8757613e86613d98565b5b613e92848285613e49565b509392505050565b600082601f830112613eaf57613eae613d93565b5b8135613ebf848260208601613e58565b91505092915050565b60008060008060008060c08789031215613ee557613ee4613aed565b5b6000613ef389828a01613bae565b9650506020613f0489828a01613bae565b955050604087013567ffffffffffffffff811115613f2557613f24613af2565b5b613f3189828a01613e9a565b945050606087013567ffffffffffffffff811115613f5257613f51613af2565b5b613f5e89828a01613e9a565b9350506080613f6f89828a01613b50565b92505060a087013567ffffffffffffffff811115613f9057613f8f613af2565b5b613f9c89828a01613e9a565b9150509295509295509295565b600080600060608486031215613fc257613fc1613aed565b5b600084013567ffffffffffffffff811115613fe057613fdf613af2565b5b613fec86828701613e9a565b935050602084013567ffffffffffffffff81111561400d5761400c613af2565b5b61401986828701613e9a565b925050604061402a86828701613bae565b9150509250925092565b61403d81613b85565b82525050565b60006020820190506140586000830184614034565b92915050565b60006020828403121561407457614073613aed565b5b600061408284828501613bae565b91505092915050565b6000602082840312156140a1576140a0613aed565b5b60006140af84828501613b50565b91505092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602160045260246000fd5b600981106140f8576140f76140b8565b5b50565b6000819050614109826140e7565b919050565b6000614119826140fb565b9050919050565b6141298161410e565b82525050565b60006040820190506141446000830185613d69565b6141516020830184614120565b9392505050565b61416181613c54565b82525050565b600060208201905061417c6000830184614158565b92915050565b600067ffffffffffffffff82111561419d5761419c613d9d565b5b602082029050602081019050919050565b600080fd5b60006141c66141c184614182565b613dfd565b905080838252602082019050602084028301858111156141e9576141e86141ae565b5b835b8181101561423057803567ffffffffffffffff81111561420e5761420d613d93565b5b80860161421b8982613e9a565b855260208501945050506020810190506141eb565b5050509392505050565b600082601f83011261424f5761424e613d93565b5b813561425f8482602086016141b3565b91505092915050565b6000806040838503121561427f5761427e613aed565b5b600083013567ffffffffffffffff81111561429d5761429c613af2565b5b6142a98582860161423a565b92505060206142ba85828601613b50565b9150509250929050565b600080604083850312156142db576142da613aed565b5b60006142e985828601613bae565b92505060206142fa85828601613b50565b9150509250929050565b600082825260208201905092915050565b7f4f6e6c7920706c6174666f726d2063616e2063616c6c20746869732e00000000600082015250565b600061434b601c83614304565b915061435682614315565b602082019050919050565b6000602082019050818103600083015261437a8161433e565b9050919050565b7f496e76616c6964206f726967696e61746f7220616464726573732e0000000000600082015250565b60006143b7601b83614304565b91506143c282614381565b602082019050919050565b600060208201905081810360008301526143e6816143aa565b9050919050565b7f496e76616c696420627579657220636f6e6669726d657220616464726573732e600082015250565b6000614423602083614304565b915061442e826143ed565b602082019050919050565b6000602082019050818103600083015261445281614416565b9050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b600061449382613b2f565b915061449e83613b2f565b92508282039050818111156144b6576144b5614459565b5b92915050565b60006144c782613b2f565b91506144d283613b2f565b92508282019050808211156144ea576144e9614459565b5b92915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b600061452a82613b2f565b915061453583613b2f565b925082614545576145446144f0565b5b828204905092915050565b600061455b82613b2f565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff820361458d5761458c614459565b5b600182019050919050565b6000819050919050565b6145b36145ae82613c54565b614598565b82525050565b60006145c582846145a2565b60208201915081905092915050565b7f4f6e6c792074686520627579657220636f6e6669726d65722063616e2077697460008201527f68647261772074686520627579657220636f6e6669726d65722072657761726460208201527f2e00000000000000000000000000000000000000000000000000000000000000604082015250565b6000614656604183614304565b9150614661826145d4565b606082019050919050565b6000602082019050818103600083015261468581614649565b9050919050565b7f546865206f72646572206973206e6f7420696e207573652e0000000000000000600082015250565b60006146c2601883614304565b91506146cd8261468c565b602082019050919050565b600060208201905081810360008301526146f1816146b5565b9050919050565b7f5468652072656d61696e696e67207374616b65206973206e6f7420656e6f756760008201527f6820666f7220612062617463682e000000000000000000000000000000000000602082015250565b6000614754602e83614304565b915061475f826146f8565b604082019050919050565b6000602082019050818103600083015261478381614747565b9050919050565b600081905092915050565b7f19457468657265756d205369676e6564204d6573736167653a0a333200000000600082015250565b60006147cb601c8361478a565b91506147d682614795565b601c82019050919050565b600081905092915050565b60006147f782613cb7565b61480181856147e1565b9350614811818560208601613cd3565b80840191505092915050565b6000614828826147be565b915061483482846147ec565b915081905092915050565b600060ff82169050919050565b6148558161483f565b82525050565b60006080820190506148706000830187614158565b61487d602083018661484c565b61488a6040830185614158565b6148976060830184614158565b95945050505050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4f6e6c79207468652073656c6c65722063616e2077697468647261772074686560008201527f206f776e6572207265776172642e000000000000000000000000000000000000602082015250565b600061492b602e83614304565b9150614936826148cf565b604082019050919050565b6000602082019050818103600083015261495a8161491e565b9050919050565b7f4f6e6c7920706c6174666f726d206163636f756e742063616e2064657374726f60008201527f792074686520636f6e74726163742e0000000000000000000000000000000000602082015250565b60006149bd602f83614304565b91506149c882614961565b604082019050919050565b600060208201905081810360008301526149ec816149b0565b9050919050565b7f546865726520617265207374696c6c20616374697665206f72646572732e0000600082015250565b6000614a29601e83614304565b9150614a34826149f3565b602082019050919050565b60006020820190508181036000830152614a5881614a1c565b9050919050565b7f546865726520617265207374696c6c20756e6578747261637465642070726f6660008201527f6974732e00000000000000000000000000000000000000000000000000000000602082015250565b6000614abb602483614304565b9150614ac682614a5f565b604082019050919050565b60006020820190508181036000830152614aea81614aae565b9050919050565b7f52696768747320636f6e6669726d6174696f6e20686173206e6f74206265656e60008201527f20636f6d706c657465642e000000000000000000000000000000000000000000602082015250565b6000614b4d602b83614304565b9150614b5882614af1565b604082019050919050565b60006020820190508181036000830152614b7c81614b40565b9050919050565b7f4f7264657220697320696e20612066696e616c20737461746520616e6420636160008201527f6e6e6f742062652063616e63656c6c65642e0000000000000000000000000000602082015250565b6000614bdf603283614304565b9150614bea82614b83565b604082019050919050565b60006020820190508181036000830152614c0e81614bd2565b9050919050565b7f5468652073656e742076616c756520646f6573206e6f74206d6174636820746860008201527f6520726563686172676520616d6f756e742e0000000000000000000000000000602082015250565b6000614c71603283614304565b9150614c7c82614c15565b604082019050919050565b60006020820190508181036000830152614ca081614c64565b9050919050565b7f5468652073656e742076616c756520646f6573206e6f74206d6174636820746860008201527f65207374616b6520616d6f756e742e0000000000000000000000000000000000602082015250565b6000614d03602f83614304565b9150614d0e82614ca7565b604082019050919050565b60006020820190508181036000830152614d3281614cf6565b9050919050565b7f546865207374616b6520616d6f756e74206973206c657373207468616e20746860008201527f65206d696e696d756d207265717569726564207374616b652e00000000000000602082015250565b6000614d95603983614304565b9150614da082614d39565b604082019050919050565b60006020820190508181036000830152614dc481614d88565b9050919050565b7f54686520627579657220636f756e742073686f756c64206c657373207468616e60008201527f20746865206d617853697a655468726573686f6c642e00000000000000000000602082015250565b6000614e27603683614304565b9150614e3282614dcb565b604082019050919050565b60006020820190508181036000830152614e5681614e1a565b9050919050565b7f4f6e6c79206578697374696e6720706c6174666f726d206163636f756e74206360008201527f616e206368616e67652074686520706c6174666f726d206163636f756e742e00602082015250565b6000614eb9603f83614304565b9150614ec482614e5d565b604082019050919050565b60006020820190508181036000830152614ee881614eac565b9050919050565b7f4f6e6c7920746865206f726967696e61746f722063616e20776974686472617760008201527f20746865206f726967696e61746f72207265776172642e000000000000000000602082015250565b6000614f4b603783614304565b9150614f5682614eef565b604082019050919050565b60006020820190508181036000830152614f7a81614f3e565b9050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603160045260246000fd5b7f496e76616c6964207369676e6174757265206c656e6774680000000000000000600082015250565b6000614fe6601883614304565b9150614ff182614fb0565b602082019050919050565b6000602082019050818103600083015261501581614fd9565b9050919050565b60006150278261483f565b91506150328361483f565b9250828201905060ff81111561504b5761504a614459565b5b9291505056fea264697066735822122056cbc0eff75e31647112db7d976a25908ba4fd807d1ff73046408de3202ad41b64736f6c6343000811003300000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000100000000000000000000000027d4c39244f26c157b5a87898569ef4ce5807413");
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
    const privateKeyBuf2 = Secp256k1.uint256("7089ec759881d637501df388b6814c45579d643e36b90df36e59576b0648682c", 16)
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
    
