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

function PostCode(data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '801',
        path: '/zjchain/get_all_nodes_bls_info/',
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

const args = process.argv.slice(2)
var tmp_id = args[1]

function get_all_nodes_bls_info(args) {
    PostCode({
        'elect_height': 0,
        'offset': 0,
        'step': 1,
    });
}

get_all_nodes_bls_info(args);