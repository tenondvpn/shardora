const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path
const { randomBytes } = require('crypto')
const Secp256k1 = require('./secp256k1_1')
const keccak256 = require('keccak256')
var querystring = require('querystring');
var http = require('http');
var fs = require('fs');
const util = require('util')
let co = require('co');

const kTestSellerCount = 11;  // real: kTestSellerCount - 10
const kTestBuyerCount = 11;  // real: kTestBuyerCount - 10
const contract_address = "48e1eab96c9e759daa3aff82b40e77cd615a41d0";

{
    const newLog = function () {
      console.info(new Date().toLocaleString());
      arguments.callee.oLog.apply(this, arguments);
    };
    const newError = function () {
      console.info(new Date().toLocaleString());
      arguments.callee.oError.apply(this, arguments);
    };
    newLog.oLog = console.log;
    newError.oError = console.error;
    console.log = newLog;
    console.error = newError;
}

function GetValidHexString(uint256_bytes) {
    var str_res = uint256_bytes.toString(16)
    while (str_res.length < 64) {
        str_res = "0" + str_res;
    }

    return str_res;
}

function create_tx(str_prikey, to, amount, gas_limit, gas_price, prepay, tx_type, key="", value="") {
    var privateKeyBuf = Secp256k1.uint256(str_prikey, 16)
    var from_private_key = Secp256k1.uint256(privateKeyBuf, 16)
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var from_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(from_private_key)
    var frompk = '04' + from_public_key.x.toString(16) + from_public_key.y.toString(16);
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

    var buffer_array = [Buffer.from(gid, 'hex'),
        Buffer.from(frompk, 'hex'),
        Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, prepay_buf];
    if (key != null && key != "") {
        buffer_array.push(Buffer.from(key));
        if (value != null && value != "") {
            buffer_array.push(Buffer.from(value));
        }
    }

    var message_buf = Buffer.concat(buffer_array);
    
    var kechash = keccak256(message_buf)
    var digest = Secp256k1.uint256(kechash, 16)
    var sig = Secp256k1.ecsign(from_private_key, digest)
    var sigR = Secp256k1.uint256(sig.r, 16)
    var sigS = Secp256k1.uint256(sig.s, 16)
    var pubX = Secp256k1.uint256(from_public_key.x, 16)
    var pubY = Secp256k1.uint256(from_public_key.y, 16)
    var data = {
        'gid': gid,
        'pubkey': '04' + from_public_key.x.toString(16) + from_public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': 3,
        "key": key,
        "val": value,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
        'pepay': prepay
    }

    var post_data = querystring.stringify(data);
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

    post_req.write(post_data);
    post_req.end();
}

function PostCode(path, data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '801',
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
            console.log('Response: ' + chunk);
        })
    });

    //console.log("req data: " + post_data);
    post_req.write(post_data);
    post_req.end();
}

function GetCode(path) {
    var post_options = {
        host: '127.0.0.1',
        port: '801',
        path: path,
        method: 'GET',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': 0
        }
    };

    var post_req = http.request(post_options, function (res) {
        res.setEncoding('utf8');
        res.on('data', function (chunk) {
            console.log('Response: ' + chunk);
        })
    });

    //console.log("req data: " + post_data);
    post_req.end();
}

function get_all_nodes_bls_info(args) {
    PostCode('/zjchain/get_all_nodes_bls_info/', {
        'elect_height': parseInt(args[1]),
        'offset': parseInt(args[2]),
        'step': parseInt(args[3]),
    });
}

function get_tx_list(args) {
    PostCode('/zjchain/transactions/', {
        'search': "",
        'height': -1,
        'shard': -1,
        'pool': -1,
        'type': parseInt(args[1]),
        'limit': args[2],
    });
}

function get_address_info(args) {
    GetCode('/zjchain/get_balance/' + args[1] + "/");
}

function get_accounts(args) {
    PostCode('/zjchain/accounts/', {
        'search': "",
        'shard': -1,
        'pool': -1,
        'order': "order by balance desc",
        'limit': "0,100",
    });
}

function get_block_detail(args) {
    GetCode('/zjchain/get_block_detail/' + args[1] + "/");
}

function get_confirm_tx_list(args) {
    PostCode('/zjchain/transactions/', {
        'search': "a0793c84fb3133c0df1b9a6ccccbbfe5e7545138",
        'height': -1,
        'shard': -1,
        'pool': -1,
        'type': parseInt(args[1]),
        'limit': args[2],
    });
}

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

const args = process.argv.slice(2)
if (args[0] == "0") {
    get_all_nodes_bls_info(args);
}

if (args[0] == "1") {
    get_tx_list(args);
}

if (args[0] == "2") {
    get_address_info(args);
}

if (args[0] == "3") {
    get_accounts(args);
}

if (args[0] == "4") {
    get_block_detail(args);
}

if (args[0] == "5") {
    create_tx("cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", "a0793c84fb3133c0df1b9a6ccccbbfe5e7545138", 0, 100000, 1, 0, 0, "key", "confirm data")
    sleep(2000)
    get_confirm_tx_list(args);
}