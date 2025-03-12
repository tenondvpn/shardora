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
const kTestSellerCount = 11;  // real: kTestSellerCount - 10
const kTestBuyerCount = 11;  // real: kTestBuyerCount - 10
var contract_address = "000feab96c9e759daa3aff82b40e77cd615a41d9";
var http_response = "";

var node_host = "127.0.0.1"

// {
//     const newLog = function () {
//       console.info(new Date().toLocaleString());
//       arguments.callee.oLog.apply(this, arguments);
//     };
//     const newError = function () {
//       console.info(new Date().toLocaleString());
//       arguments.callee.oError.apply(this, arguments);
//     };
//     newLog.oLog = console.log;
//     newError.oError = console.error;
//     console.log = newLog;
//     console.error = newError;
// }
  
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
                //console.log('Response: ' + chunk + ", " + data);
            } else {
                //console.log('Response: ' + chunk + ", " + data);
            }
        })
    });

    ////console.log("req data: " + post_data);
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

function param_contract(str_prikey, tx_type, gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay) {
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

    var message_buf = Buffer.concat([
        Buffer.from(gid, 'hex'), 
        Buffer.from(frompk, 'hex'), 
        Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, 
        Buffer.from(contract_bytes, 'hex'), 
        Buffer.from(input, 'hex'), 
        prepay_buf]);
    var kechash = keccak256(message_buf)
    var digest = Secp256k1.uint256(kechash, 16)
    var sig = Secp256k1.ecsign(from_private_key, digest)
    var sigR = Secp256k1.uint256(sig.r, 16)
    var sigS = Secp256k1.uint256(sig.s, 16)
    var pubX = Secp256k1.uint256(from_public_key.x, 16)
    var pubY = Secp256k1.uint256(from_public_key.y, 16)
    var isValidSig = Secp256k1.ecverify(pubX, pubY, sigR, sigS, digest)
    //console.log("gid: " + gid.toString(16))
    if (!isValidSig) {
        //console.log('signature transaction failed.')
        return;
    }

    return {
        'gid': gid,
        'pubkey': '04' + from_public_key.x.toString(16) + from_public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': 3,
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

function create_tx(str_prikey, to, amount, gas_limit, gas_price, prepay, tx_type) {
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

    var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, prepay_buf]);
    var kechash = keccak256(message_buf)
    var digest = Secp256k1.uint256(kechash, 16)
    var sig = Secp256k1.ecsign(from_private_key, digest)
    var sigR = Secp256k1.uint256(sig.r, 16)
    var sigS = Secp256k1.uint256(sig.s, 16)
    var pubX = Secp256k1.uint256(from_public_key.x, 16)
    var pubY = Secp256k1.uint256(from_public_key.y, 16)
    //console.log("gid: " + gid.toString(16))
    return {
        'gid': gid,
        'pubkey': '04' + from_public_key.x.toString(16) + from_public_key.y.toString(16),
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

function new_contract(from_str_prikey, contract_bytes) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var kechash = keccak256(from_str_prikey + gid + contract_bytes).toString('hex')
    var data = param_contract(
        from_str_prikey,
        6,
        gid,
        contract_address,
        0,
        10000000,
        1,
        contract_bytes,
        "",
        0);
    PostCode(data);
    return contract_address;
}

function call_contract(str_prikey, input, amount) {
    //console.log("contract_address: " + contract_address);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = param_contract(
        str_prikey,
        8,
        gid,
        contract_address,
        amount,
        90000000000,
        1,
        "",
        input,
        0);
    PostCode(data);
}

function QueryPostCode(path, data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '127.0.0.1',
        port: '23001',
        path: path,
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': Buffer.byteLength(post_data)
        }
    };

    var post_req = http.request(post_options, function (res) {
        res.setEncoding('utf8');
        var data = '';
        res.on('data', function(chunk) {
            data += chunk;
        });
        
        res.on('end', function() {
            http_response = data;
        });
    });

    post_req.write(post_data);
    post_req.end();
}

function QueryContract(str_prikey, input) {
    var privateKeyBuf = Secp256k1.uint256(str_prikey, 16)
    var self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
    var self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
    var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
    var address = keccak256(pk_bytes).toString('hex')
    var address = address.slice(address.length - 40, address.length)
    var data = {
        "input": input,
        'address': contract_address,
        'from': address,
    };

    QueryPostCode('/query_contract', data);
}

function Prepayment(str_prikey, prepay) {
    var data = create_tx(str_prikey, contract_address, 0, 100000, 1, prepay, 7);
    PostCode(data);
}

function Prepayment(str_prikey, prepay) {
    var data = create_tx(str_prikey, contract_address, 0, 100000, 1, prepay, 7);
    PostCode(data);
}

async function SetManagerPrepayment(contract_address, prikey) {
    // 为管理账户设置prepayment
    Prepayment(prikey, 1000000000000);
    var account1 = web3.eth.accounts.privateKeyToAccount(
        '0x' + prikey);
    var check_accounts_str = "";
    check_accounts_str += "'" + account1.address.toString('hex').toLowerCase().substring(2) + "'"; 
    var check_count = 1;
    var cmd = `clickhouse-client --host ${node_host} --port 9000 -q "select count(distinct(user)) from zjc_ck_prepayment_table where contract='${contract_address}' and user in (${check_accounts_str});"`;
    const { exec } = require('child_process');
    const execPromise = util.promisify(exec);
    // 检查合约是否创建成功
    var try_times = 0;
    while (try_times < 30) {
        try {
            const {stdout, stderr} = await execPromise(cmd);
            if (stdout.trim() == check_count.toString()) {
                console.error(`${cmd} contract prepayment success: ${stdout}`);
                break;
            }

            //console.log(`${cmd} contract prepayment failed error: ${stderr} count: ${stdout}`);
        } catch (error) {
            //console.log(error);
        }

        ++try_times;
        await sleep(2000);
    }

    if (try_times >= 30) {
        console.error(`contract prepayment failed!`);
        return;
    }
}

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function InitC2cEnv(key, value) {
    const { exec } = require('child_process');
    exec('solc --bin ./exchange.sol', async (error, stdout, stderr) => {
        if (error) {
          console.error(`exec error: ${error}`);
          return;
        }

        var out_lines = stdout.split('\n');
        //console.log(`solc bin codes: ${out_lines[3]}`);
        {
            // 转账到管理账户，创建合约
            {
                new_contract(
                    "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
                    out_lines[3]);
                var contract_cmd = `clickhouse-client --host ${node_host} --port 9000 -q  "SELECT to FROM zjc_ck_account_key_value_table where type = 6 and key in ('5f5f6b437265617465436f6e74726163744279746573436f6465',  '5f5f6b437265617465436f6e74726163744279746573436f6465') and to='${contract_address}' limit 1;"`
                var try_times = 0;
                // 检查合约是否创建成功
                const execPromise = util.promisify(exec);
                while (try_times < 30) {
                    try {
                    // wait for exec to complete
                        const {stdout, stderr} = await execPromise(contract_cmd);
                        if (stdout.trim() == contract_address) {
                            //console.log(`create contract success ${stdout}`);
                            break;
                        }
                            
                        //console.log(`create contract failed ${stderr}`);
                    } catch (error) {
                        //console.log(error);
                    }

                    ++try_times;
                    await sleep(2000);
                }

                if (try_times >= 30) {
                    console.error(`create contract address failed!`);
                    return;
                }

                // 预设值合约调用币，并等待成功
                var prikeys = [
                    "b3d3f5f12e99c7f03755501dbe29ed0b28d9bfd19fde14a8f41b0f7b29364330",
                ];
                for (var i = 0; i < prikeys.length; ++i)
                {
                    SetManagerPrepayment(contract_address, prikeys[i]);
                }

            }
        }
      });
}

function CreateNewItem(hash, info, price, start, end) {
    // bytes32 hash, bytes memory info, uint256 price, uint256 start, uint256 end
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes', 'uint256', 'uint256', 'uint256'], 
        [hash, info, price, start, end]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('CreateNewItem(bytes32,bytes,uint256,uint256,uint256)');
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0);
}

function PurchaseItem(hash, price) {
    // bytes32 hash, bytes memory info, uint256 price, uint256 start, uint256 end
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32'], 
        [hash]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('PurchaseItem(bytes32)');
    call_contract(
        "286a4972ad6f5d7ed74715847f6b03b238b4bdc946796abac09784f8310f7f6d", 
        addParamCode.substring(2) + addParam.substring(2), price);
}

function ConfirmPurchase(hash) {
    // bytes32 hash, bytes memory info, uint256 price, uint256 start, uint256 end
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32'], 
        [hash]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('ConfirmPurchase(bytes32)');
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0);
}


function hexStringToInt64(hexString) {
    // 将16进制字符串转换为Buffer
    const buffer = Buffer.from(hexString, 'hex');
    // 如果buffer大小大于8，则截取前8个字节
    if (buffer.length > 8) {
        buffer.slice(0, 8);
    }
    // 使用DataView以64位整数形式读取
    const dataView = new DataView(buffer.buffer.slice(0, 8));
    return dataView.getBigInt64(0);
}

async function GetAllItemJson() {
    http_response = "";
    var addParam = web3.eth.abi.encodeParameters(
        ['uint256', 'uint256'], 
        [0, 100]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('GetAllItemJson(uint256,uint256)');
    QueryContract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2));

    while (http_response == "") {
        console.log("waiting...");
        await sleep(1000);
    }

    console.log("http_response: " + http_response);
    // var json_test = '[{"id":"00","hash":"3e07c561e4a40074e6344f1a62ad739be146ea945f0fda414d459a70b87d8a5a","owner":"44a5c714cb3f502fb77618a4a0353d96148fde7e","info":"746573745f6a736f6e","price":"01","start_time":"00","end_time":"00","buyers":[{"buyer":"0000000000000000000000000000000000000000","price":"00"},{"buyer":"611cf0f0a69ef9c74ef36d2e0892280dc4494fe5","price":"64"}]}]';
    // var res_json_tmp = JSON.parse(json_test);


    var res_json = JSON.parse(http_response);
    for (var i = 0; i < res_json.length; ++i) {
        res_json[i].id = hexStringToInt64(res_json[i].id);
        res_json[i].price = hexStringToInt64(res_json[i].price);
        res_json[i].start_time = hexStringToInt64(res_json[i].start_time);
        res_json[i].end_time = hexStringToInt64(res_json[i].end_time);
        for (var j = 0; j < res_json[i].buyers.length; ++j) {
            res_json[i].buyers[j].price = hexStringToInt64(res_json[i].buyers[j].price);
        }
    }


    console.log(res_json);
}

const args = process.argv.slice(2)
if (process.env.contract_address != null) {
    contract_address = process.env.contract_address;
    //console.log("use env contract address: " + contract_address)
}

// SetUp：初始化算法，需要用到pbc库
if (args[0] == 0) {
    var pairing_param_value = "type a\nq 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\nh 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\nr 730750818665451621361119245571504901405976559617\nexp2 159\nexp1 107\nsign1 1\nsign0 1";
    InitC2cEnv("c0", pairing_param_value);
}

var tmp_id = args[1]
// 测试聚合环签名整个流程
var id = keccak256('cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848' + contract_address + tmp_id).toString('hex');
console.log(id);
if (args[0] == 1) {
    CreateNewItem('0x'+id, web3.utils.toHex("test_json"), 1, 0, 0);
    //console.log(id);
}

if (args[0] == 2) {
    PurchaseItem('0x'+id, 100);
    //console.log(id);
}

if (args[0] == 3) {
    ConfirmPurchase('0x'+id)
}

// 测试合约查询
if (args[0] == 30) {
    GetAllItemJson();
}
