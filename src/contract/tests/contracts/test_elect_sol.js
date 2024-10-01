const Web3 = require('web3');
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path
const { randomBytes } = require('crypto');
const Secp256k1 = require('../secp256k1.js');
const keccak256 = require('keccak256');
var querystring = require('querystring');
var http = require('http');
var fs = require('fs');
const { get } = require('https');
const assert = require('assert');
var utils = require('./test_utils.js');
const { Console } = require('console');
const { mainModule, argv } = require('process');
const PRIVATE_KEY = "e615ecc79c347e2cfd5cb3fcb85a73354564f93cf644f3217c7251f6bd9db350";


// 3bacf0f9ff4285d55c0cec854c3f715cf5cacf966fd0942ec7b241b639038446
const CONTRACT_BYTES = fs.readFileSync('ElectPledgeContract.bin', 'utf-8');
// key : shardID, value : ip:port
var nodes = {
   0 : "128.0.0.1:8201", 
   1 : "127.0.0.1:8201",
   2 : "127.0.0.1:8201",
   3 : "127.0.0.1:8301",
   4 : "127.0.0.1:8401",
   5 : "127.0.0.1:8501",
   6 : "127.0.0.1:8601"
    
}
// key : shardID, value : privateKey(节点的私钥)
var shard_prikey = {
    0 : "e**************************************************************2",
    1 : "f**************************************************************3",
    2 : "da7be86dd2ed428f062892f434a8f5a547f9a141bfd2caf95293769c03fc13dd",
    3 : "e615ecc79c347e2cfd5cb3fcb85a73354564f93cf644f3217c7251f6bd9db350",
    4 : "58159d564fcedd8165e59fe51d8dbd33591fc5256e13b4226eb9058f4700dceb",
    5 : "e0b1f61533052d7bb821cfa4b8e18af890142efed1ddd620d26b875c023b1982",
    6 : "6784ee1d31d496763232f9008461e3cc45f9edeedd0c9e8893199249a07f791e"
}

function init_private_key(sk) {
    const privateKeyBuf = Secp256k1.uint256(sk, 16);
    var self_private_key = Secp256k1.uint256(privateKeyBuf, 16);
    var self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key);
    var pk_bytes = utils.hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16));
    var address = keccak256(pk_bytes).toString('hex');
    address = address.slice(address.length - 40, address.length);
    res = {
        "sk": self_private_key,
        "pk": self_public_key,
        "account_id": address,
    };
    console.log("init_private_key: " + JSON.stringify(res, null, 2));
    return res;
}
function create_simple_contract(des_shard_id) {
    let keypair = init_private_key(shard_prikey[des_shard_id]);
    
    let node_str = nodes[des_shard_id]; 
    let account_id = keypair["account_id"];
    var contract_addr = new_contract(account_id, CONTRACT_BYTES, node_str, des_shard_id, keypair);
    const opt = { flag: 'w', }
    fs.writeFile('contract_address', contract_addr, opt, (err) => {
        if (err) {
            console.error(err)
        }
    })
}
function param_contract(tx_type, gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay, des_shard_id, keypair) {
    var public_key = keypair["pk"];
    var private_key = keypair["sk"];

    var gid = utils.GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var frompk = '04' + public_key.x.toString(16) + public_key.y.toString(16);
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
    const sig = Secp256k1.ecsign(private_key, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(public_key.x, 16)
    const pubY = Secp256k1.uint256(public_key.y, 16)
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
        'pubkey': '04' + public_key.x.toString(16) + public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': des_shard_id,
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
function QueryContract(contract_address, input, from_node, keypair, callback) {
    let account_id = keypair["account_id"];
    var data = {
        "input": input,
        'address': contract_address,
        'from': account_id,
    };

    console.log("query contract", JSON.stringify(data));

    return QueryPostCode('/query_contract', data, from_node, callback);
}
function QueryPostCode(path, data, from_node, callback) {
    var node = parseFromNode(from_node);
    var post_data = querystring.stringify(data);
    var post_options = {
        host: node["ip"],
        port: node["port"],
        path: path,
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': Buffer.byteLength(post_data)
        }
    };

    var post_req = http.request(post_options, function (res) {
        res.setEncoding('utf8');
        var body = '';
        res.on('data', function (chunk) {
            console.log("body: " + body)
            body += chunk;
        });
        res.on('end', function () {
            try {
                console.log("body: " + body);
                var json_res = JSON.parse(body);
                callback(json_res);
            } catch (error) {
                console.log(error);
                callback("");
            }
        });
    });

    post_req.on('error', function (e) {
        console.log(`Problem with request: ${e.message}`);
    });

    post_req.write(post_data);
    post_req.end();
}

function call_contract(contract_address, input, amount, from_node, des_shard_id, keypair) {
    console.log("call contract", JSON.stringify(contract_address), input, amount);
    var gid = utils.GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = param_contract(
        8,
        gid,
        contract_address,
        amount,
        1000*1000*1000*10,
        1,
        "",
        input,
        1000*1000*1000*1000, des_shard_id, keypair);
        console.log("call contract data: " + JSON.stringify(data, null, 4));
    PostCode(data, from_node);
}


function gen_elect_plege_contract_addr(shard_id) {
    const ELECT_PLEGE_CONTRACT_ADDR = "1397000000000000000000000000000000000000"; // 请替换为实际的合约地址
    let width = 6;
    let hexString = shard_id.toString(16);
    while (hexString.length < width) {
        hexString = '0' + hexString;
    }
 
    return ELECT_PLEGE_CONTRACT_ADDR.slice(0, -hexString.length) + hexString;
}

function new_contract(account_id, contract_bytes, from_node, des_shard_id, keypair) {
    var gid = utils.GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    // var contract_address = gen_contract_address(account_id, gid, contract_bytes);
    var contract_address = gen_elect_plege_contract_addr(des_shard_id);
    console.log("new contract address: " + contract_address);
    var data = param_contract(
        6, // tx_type
        gid,
        contract_address, // to
        0, // amount
        100000000000, // gas_limit
        1,// gas_price
        contract_bytes, // input
        "", //
        100000000000, // prepay
        des_shard_id, keypair);
    PostCode(data, from_node);

    return contract_address;
}

function parseFromNode(from_node) {
    var [from_ip, from_port_str] = from_node.split(":");
    var from_port = parseInt(from_port_str, 10);
    return {
        "ip": from_ip,
        "port": from_port,
    }
}

function PostCode(data, from_node) {
    var node = parseFromNode(from_node);
    var post_data = querystring.stringify(data);
    var post_options = {
        host: node["ip"],
        port: node["port"],
        path: '/transaction',
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': Buffer.byteLength(post_data)
        }
    };
    console.log("post_options: " + JSON.stringify(post_options));

    var post_req = http.request(post_options, function (res) {
        res.setEncoding('utf8');
        res.on('data', function (chunk) {
            if (chunk != "ok") {
                // llog('Response: ' + chunk + ", " + data);
            } else {
                // llog('Response: ' + chunk + ", " + data);
            }
        })
    });

    post_req.write(post_data);
    post_req.end();
}

function gen_contract_address(account_id, gid, contract_bytes) {
    var kechash = keccak256(account_id + gid + contract_bytes).toString('hex');
    var res = kechash.slice(kechash.length - 40, kechash.length);
    console.log("new contract address: " + res);
    return res;
}

function str_to_hex(str) {
    var arr1 = [];
    for (var n = 0; n < str.length; n++) {
        var hex = Number(str.charCodeAt(n)).toString(16);
        arr1.push(hex);
    }
    return arr1.join('');
}

function main() {
    const args = process.argv.slice(2);
    // 创建合约, 合约地址为 1397000000000000000000000000xx ,xx 为 shardid
    // arg[1] 为需要创建的 shard 的 shardid
    // 如果没有第二个参数,则为所有定义的shard创建合约
    if (args[0] == 0) {
        if (args.length == 1) {
           for(var i = 2; i<=6; i++) {
            create_simple_contract(i);
           }
           return;
        }
        let des_shard_id = args[1];
        create_simple_contract(des_shard_id);

    }
    // 有时调用合约会提示没有 prepay, 可以尝试调用这个方法 
    if (args[0] == "p") {
        var contract_addr = fs.readFileSync('contract_address', 'utf-8');
        let des_shard_id = 3;
        var prki = args[1];
        var keypair = init_private_key(prki);

        var amount=0 ;
        var gid = utils.GetValidHexString(Secp256k1.uint256(randomBytes(32)));
        var data = param_contract(
            7,
            gid,
            contract_addr,
            amount,
            1000000000,
            1,
            "",
            "",
            1000000000, des_shard_id, keypair);
            console.log("call contract data: " + JSON.stringify(data, null, 4));
        PostCode(data, nodes[3]);
    } 
    const abi = JSON.parse(fs.readFileSync('./ElectPledgeContract.abi', 'utf-8'));

    const contract = new web3.eth.Contract(abi);
    // 质押接口,
    //args[1] 为需要质押的shardid,如果没有则为所有分片节点进行质押. 
    if (args[0] == "ps") {

        data = contract.methods.pledge().encodeABI().substring(2);
        amount = 1111;

        if (args.length == 1) {
            for(var des_shard_id = 2; des_shard_id<=6; des_shard_id++) {
                var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
                var keypair = init_private_key(shard_prikey[des_shard_id]);
                call_contract(contract_addr, data, amount, nodes[des_shard_id], des_shard_id, keypair);
            }
       } else {
            var des_shard_id = args[1];
            var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
            var keypair = init_private_key(shard_prikey[des_shard_id]);
            call_contract(contract_addr, data, amount, nodes[des_shard_id], des_shard_id, keypair);
        }
        return;

    }
    // 获取 节点在对应 shard 中所有有效的(冻结的)质押币.
    // 只有冻结了的质押币,才可以作为选举权重.
    if(args[0] == "gs") {

        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);
        var data = contract.methods.getAccountStakesElect(keypair.account_id).encodeABI().substring(2);

        console.log("data: " + data.toString('hex'));

        QueryContract(contract_addr, data, nodes[des_shard_id], keypair, (a) => {});

      
    }
    // 获取对应 shard 的当前选举高度
    if(args[0] == "ge") {
        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);
        var data = contract.methods.getNowElectHeight().encodeABI().substring(2);

        console.log("data: " + data.toString('hex'));

        QueryContract(contract_addr, data, nodes[des_shard_id], keypair, (a) => {});

      
    }


    if(args[0] == "gaa") {
        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);
        var data = contract.methods.getAllAddresses().encodeABI().substring(2);

        console.log("data: " + data.toString('hex'));

        QueryContract(contract_addr, data, nodes[des_shard_id], keypair, (a) => {});

      
    }

    if(args[0] == "gae") {
        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);
        var data = contract.methods.getAllEh(keypair.account_id).encodeABI().substring(2);

        console.log("data: " + data.toString('hex'));

        QueryContract(contract_addr, data, nodes[des_shard_id], keypair, (a) => {});

      
    }
    
    // 获取当前节点在对应 shard 中所有已质押的币 
    if(args[0] == "gts") {
        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);
        var data = contract.methods.getTotalStake(keypair.account_id).encodeABI().substring(2);

        console.log("data: " + data.toString('hex'));

        QueryContract(contract_addr, data, nodes[des_shard_id], keypair, (a) => {});

      
    } 
    // 提取当前节点所有已解冻的质押币
    if(args[0] == "wd") {
        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);
        var data = contract.methods.withDraw().encodeABI().substring(2);
        var amount = 0;
        console.log("data: " + data.toString('hex'));

        call_contract(contract_addr, data, amount, nodes[des_shard_id], des_shard_id, keypair);;

      
    }


    if(args[0] == "se") {
        var des_shard_id = args[1];
        var contract_addr = gen_elect_plege_contract_addr(des_shard_id);
        var keypair = init_private_key(shard_prikey[des_shard_id]);

        var elect_height = args[2];
        var data = contract.methods.setElectHeight(elect_height).encodeABI().substring(2);
        var amount = 0;
        console.log("data: " + data.toString('hex'));

        call_contract(contract_addr, data, amount, nodes[des_shard_id], des_shard_id, keypair);;

      
    }
   


}



main();


