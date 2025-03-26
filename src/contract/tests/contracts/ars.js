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
var contract_address = "08e1eab96c9e759daa3aff82b40e77cd615a41d5";
var node_host = "127.0.0.1"

/*
{
    const newLog = function () {
      console.info(new Date().toLocaleString());
      arguments.callee.oLog.apply(this, arguments);
    };
    const newError = function () {
      console.info(new Date().toLocaleString());
      arguments.callee.oError.apply(this, arguments);
    };
    newLog.oLog = //console.log;
    newError.oError = console.error;
    //console.log = newLog;
    console.error = newError;
}
*/
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

function do_transaction(str_prikey, to_addr, amount, gas_limit, gas_price) {
    var data = create_tx(str_prikey, to_addr, amount, gas_limit, gas_price, 0, 0);
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
        res.on('data', function (chunk) {
            //console.log('Response: ' + chunk);
            var json_res = JSON.parse(chunk)
            // //console.log('amount: ' + json_res.amount + ", tmp: " + json_res.tmp);
        })
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

async function SetManagerPrepayment(contract_address) {
    // 为管理账户设置prepayment
    Prepayment("cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 1000000000000);
    var account1 = web3.eth.accounts.privateKeyToAccount(
        '0xcefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848');
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
    exec('solc --bin ./ars.sol', async (error, stdout, stderr) => {
        if (error) {
          console.error(`exec error: ${error}`);
          return;
        }

        var out_lines = stdout.split('\n');
        //console.log(`solc bin codes: ${out_lines[3]}`);
        {
            var cons_codes = "";
            {
                var key_len = key.length.toString();
                if (key.length <= 9) {
                    key_len = "0" + key_len;
                }
    
                var param = "readd" + key_len + key + value;
                var hexparam = web3.utils.toHex(param);
                console.log("constructer param: " + hexparam);
                // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
                cons_codes = web3.eth.abi.encodeParameters(
                    ['bytes'], 
                    [hexparam]);
                //console.log("cons_codes: " + cons_codes.substring(2));
            }
            // 转账到管理账户，创建合约
            {
                new_contract(
                    "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
                    out_lines[3] + cons_codes.substring(2));
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
                SetManagerPrepayment(contract_address);
            }
        }
      });
}

function add_pairing_param(prev, key, value) {
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = prev + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes'], 
        [hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('call_proxy_reenc(bytes)');
    //console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0);

}

function CreateNewArs(prev, key, value, id) {
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = prev + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var addParam = web3.eth.abi.encodeParameters(
        ['uint256', 'uint256', 'bytes32', 'bytes'], 
        [3, 2, '0x' + id, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('CreateNewArs(uint256,uint256,bytes32,bytes)');
    //console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0);
}

function SingleSign(prev, key, value, id) {
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = prev + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes'], 
        ['0x' + id, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('SingleSign(bytes32,bytes)');
    //console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0);
}

function AggSign(prev, key, value, id) {
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = prev + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes'], 
        ['0x' + id, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('AggSign(bytes32,bytes)');
    //console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0);
}

function GetAllArsJson() {
    var addParamCode = web3.eth.abi.encodeFunctionSignature('GetAllArsJson()');
    //console.log("GetAllArsJson 0: " + addParamCode.substring(2));
    QueryContract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2));
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
    CreateNewArs("tarscr", "tarscr", args[2]+"-"+args[3]+"," + id, id);
    //console.log(id);
}

if (args[0] == 2) {
    SingleSign("tarsps", "tarsps", args[2] + "-" + id, id);
}

//--ripemd_val "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b789000000-cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b789000000"
if (args[0] == 3) {
    AggSign("tarsas", "tarsas", id, id);
}

if (args[0] == 4) {
    add_pairing_param("tars", "tars", id, id);
}

// 测试合约查询
if (args[0] == 30) {
    GetAllArsJson();
}
