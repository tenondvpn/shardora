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