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
const contract_address = "48e1eab96c9e759daa3aff82b40e77cd615a41d3";

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
    console.log("digest: " + digest)
    console.log("sigr: " + sigR.toString(16))
    console.log("sigs: " + sigS.toString(16))
    if (!isValidSig) {
        console.log('signature transaction failed.')
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
    console.log("contract_address: " + contract_address);
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
            var json_res = JSON.parse(chunk)
            console.log('amount: ' + json_res.amount + ", tmp: " + json_res.tmp);
            console.log('Response: ' + chunk);
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
    Prepayment("20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 1000000000000);
    Prepayment("748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05", 1000000000000);
    Prepayment("b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5", 1000000000000);
    var account1 = web3.eth.accounts.privateKeyToAccount(
        '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
    var account2 = web3.eth.accounts.privateKeyToAccount(
        '0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
    var account3 = web3.eth.accounts.privateKeyToAccount(
        '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
    var check_accounts_str = "";
    check_accounts_str += "'" + account1.address.toString('hex').toLowerCase().substring(2) + "',"; 
    check_accounts_str += "'" + account2.address.toString('hex').toLowerCase().substring(2) + "',"; 
    check_accounts_str += "'" + account3.address.toString('hex').toLowerCase().substring(2) + "',"; 
    var check_count = 3;
    for (var i = 10; i < kTestSellerCount; ++i) {
        Prepayment('b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1' + i.toString(), 1000000000000);
        var account4 = web3.eth.accounts.privateKeyToAccount(
            '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1' + i.toString());
        if (i == 29) {
            check_accounts_str += "'" + account4.address.toString('hex').toLowerCase().substring(2) + "'"; 
        } else {
            check_accounts_str += "'" + account4.address.toString('hex').toLowerCase().substring(2) + "',"; 
        }

        ++check_count;
    }

    var cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q "select count(distinct(user)) from zjc_ck_prepayment_table where contract='${contract_address}' and user in (${check_accounts_str});"`;
    const { exec } = require('child_process');
    const execPromise = util.promisify(exec);
    // 检查合约是否创建成功
    var try_times = 0;
    while (try_times < 30) {
        try {
            const {stdout, stderr} = await execPromise(cmd);
            if (stdout.trim() == check_count.toString()) {
                console.error(`contract prepayment success: ${stdout}`);
                break;
            }

            console.log(`contract prepayment failed error: ${stderr} count: ${stdout}`);
        } catch (error) {
            console.log(error);
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
    exec('solc --bin ./proxy_reencyption.sol', async (error, stdout, stderr) => {
        if (error) {
          console.error(`exec error: ${error}`);
          return;
        }

        var out_lines = stdout.split('\n');
        console.log(`solc bin codes: ${out_lines[3]}`);
        {
            // 三个管理员账户
            var account1 = web3.eth.accounts.privateKeyToAccount(
                '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
            console.log("account1 :");
            console.log(account1.address.toString('hex').toLowerCase().substring(2));
            var account2 = web3.eth.accounts.privateKeyToAccount(
                '0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
            console.log("account2 :");
            console.log(account2.address.toString('hex').toLowerCase().substring(2));
            var account3 = web3.eth.accounts.privateKeyToAccount(
                '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
            console.log("account3 :");
            console.log(account3.address.toString('hex').toLowerCase().substring(2));
            var append_address = "";
            var check_count = 3;
            for (var i = 10; i < kTestSellerCount; ++i) {
                // 卖家账户设置
                var account4 = web3.eth.accounts.privateKeyToAccount(
                    '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1' + i.toString());
                do_transaction(
                    "863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 
                    account4.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                append_address += ",'" + account4.address.toString('hex').toLowerCase().substring(2) + "'";
                ++check_count;
            }

            var cons_codes = "";
            {
                var key_len = key.length.toString();
                if (key.length <= 9) {
                    key_len = "0" + key_len;
                }
    
                var param = "add" + key_len + key + value;
                var hexparam = web3.utils.toHex(param);
                // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
                cons_codes = web3.eth.abi.encodeParameters(
                    ['bytes'], 
                    [hexparam]);
                console.log("cons_codes: " + cons_codes.substring(2));
            }
            // 转账到管理账户，创建合约
            {
                do_transaction(
                    "863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 
                    account1.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                do_transaction(
                    "863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 
                    account2.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                do_transaction(
                    "863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 
                    account3.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                new_contract(
                    "863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 
                    out_lines[3] + cons_codes.substring(2));
                var contract_cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q  "SELECT to FROM zjc_ck_account_key_value_table where type = 6 and key in ('5f5f6b437265617465436f6e74726163744279746573436f6465',  '5f5f6b437265617465436f6e74726163744279746573436f6465') and to='${contract_address}' limit 1;"`
                var try_times = 0;
                // 检查合约是否创建成功
                const execPromise = util.promisify(exec);
                while (try_times < 30) {
                    try {
                    // wait for exec to complete
                        const {stdout, stderr} = await execPromise(contract_cmd);
                        if (stdout.trim() == contract_address) {
                            console.log(`create contract success ${stdout}`);
                            break;
                        }
                            
                        console.log(`create contract failed ${stderr}`);
                    } catch (error) {
                        console.log(error);
                    }

                    ++try_times;
                    await sleep(2000);
                }

                if (try_times >= 30) {
                    console.error(`create contract address failed!`);
                    return;
                }

                // 检查转账成功
                var cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q  "select id, balance from zjc_ck_account_table where id in  ('${account1.address.toString('hex').toLowerCase().substring(2)}',  '${account2.address.toString('hex').toLowerCase().substring(2)}',  '${account3.address.toString('hex').toLowerCase().substring(2)}',  '${account4.address.toString('hex').toLowerCase().substring(2)}' ${append_address});"`;
                var try_times = 0;
                while (try_times < 30) {
                    try {
                    // wait for exec to complete
                        const {stdout, stderr} = await execPromise(cmd);
                        var split_lines = stdout.trim().split('\n');
                        var dictionary = new Set();
                        console.log(`transfer to manager address split_lines.length: ${split_lines.length}`);
                        if (split_lines.length >= check_count) {
                            for (var line_idx = 0; line_idx < split_lines.length; ++line_idx) {
                                var item_split = split_lines[line_idx].split("\t");
                                var new_balance = parseInt(item_split[1].trim(), 10);
                                if (new_balance >= 1100000000000) {
                                    dictionary.add(item_split[0].trim());
                                    console.log(`transfer to manager address new_balance: ${new_balance} dictionary size ${dictionary.size}`);
                                }
                            }
                        }

                        if (dictionary.size == check_count.toString()) {
                            console.log(`transfer to manager address success error: ${stderr} count: ${stdout}`);
                            break;
                        }

                        console.log(`transfer to manager address failed error: ${stderr} count: ${stdout}`);
                    } catch (error) {
                        console.log(error);
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

function add_pairing_param(key, value) {
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = "add" + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes'], 
        [hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('call_proxy_reenc(bytes)');
//console.log("addParam 0: " + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 
        addParamCode.substring(2) + addParam.substring(2), 0);

}

function call_decrypt() {
    var param1 = "decrypt0001";
    var hexparam1 = web3.utils.toHex(param1);
    var addParam1 = web3.eth.abi.encodeParameters(
        ['bytes'], 
        [hexparam1]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('call_proxy_reenc(bytes)');
    console.log("addParam 1: " + addParamCode.substring(2) + addParam1.substring(2));
    call_contract(
        "20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 
        addParamCode.substring(2) + addParam1.substring(2), 0);
}

function set_all_params(tag) {
    try {
        if (tag == 0) {
            const data = fs.readFileSync('./params_tk', 'UTF-8');
            add_pairing_param('all', data);
        } else if (tag == 1) {
            const data = fs.readFileSync('./params_enc', 'UTF-8');
            add_pairing_param('all', data);
        }

    } catch (err) {
        console.error(err);
    }
}

function run_all() {
    add_pairing_param("CreatPath", "CreatPath");
    add_pairing_param("RKGen", "RKGen");
    add_pairing_param("Upd", "Upd");
    add_pairing_param("Enc", "Enc");
    add_pairing_param("ReEnc", "ReEnc");
    add_pairing_param("Dec", "Dec");
}

const args = process.argv.slice(2)
// SetUp：初始化算法，需要用到pbc库
if (args[0] == 0) {
    var pairing_param_value = "type a\nq 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\nh 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\nr 730750818665451621361119245571504901405976559617\nexp2 159\nexp1 107\nsign1 1\nsign0 1";
    InitC2cEnv("c0", pairing_param_value);
}

// CreatPath(i)：由用户i选择多个被委托者。按选择顺序生成一个路径（列表），其中存放被委托者的公钥。
if (args[0] == 1) {
    add_pairing_param("CreatPath", "CreatPath");
}

// RKGen：重加密密钥生成，需要用到pbc库
if (args[0] == 2) {
    add_pairing_param("RKGen", "RKGen");
}

// Upd：token更新算法，需要用到pbc库
if (args[0] == 3) {
    add_pairing_param("Upd", "Upd");
}

// Enc：加密，需要用到pbc库
if (args[0] == 4) {
    add_pairing_param("Enc", "Enc");
}

// ReEnc：重加密，需要用到pbc库 (这一步包含一个分布式随机数生成协议，即多个代理协商出一个统一的随机数)
if (args[0] == 5) {
    add_pairing_param("ReEnc", "ReEnc");
}

// Dec：解密，需要用到pbc库
if (args[0] == 6) {
    add_pairing_param("Dec", "Dec");
}

if (args[0] == 7) {
    call_decrypt()
}

if (args[0] == 8) {
    run_all()
}

// 测试合约查询
if (args[0] == 9) {
    QueryContract(
        "b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d129", 
        "cdfd45bb");
}
