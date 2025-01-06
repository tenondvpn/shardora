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

function param_contract(str_prikey, tx_type, gid, to, amount, gas_limit, gas_price, contract_bytes, input, prepay, key="", value="") {
    var privateKeyBuf = Secp256k1.uint256(str_prikey, 16)
    var from_private_key = Secp256k1.uint256(privateKeyBuf, 16)
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

    // var message_buf = Buffer.concat([
    //     Buffer.from(gid, 'hex'), 
    //     Buffer.from(frompk, 'hex'), 
    //     Buffer.from(to, 'hex'),
    //     amount_buf, gas_limit_buf, gas_price_buf, step_buf, 
    //     Buffer.from(contract_bytes, 'hex'), 
    //     Buffer.from(input, 'hex'), 
    //     prepay_buf]);

    var buffer_array = [
        Buffer.from(gid, 'hex'), 
        Buffer.from(frompk, 'hex'), 
        Buffer.from(to, 'hex'),
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, 
        Buffer.from(contract_bytes, 'hex'), 
        Buffer.from(input, 'hex'), 
        prepay_buf];
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
    var isValidSig = Secp256k1.ecverify(pubX, pubY, sigR, sigS, digest)
    console.log("gid: " + gid.toString(16))
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
        "key": key,
        "val": value,
        "input": input,
        "pepay": prepay,
        'sign_r': sigR.toString(16),
        'sign_s': sigS.toString(16),
        'sign_v': sig.v,
    }
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

    // var message_buf = Buffer.concat([Buffer.from(gid, 'hex'), Buffer.from(frompk, 'hex'), Buffer.from(to, 'hex'),
    //     amount_buf, gas_limit_buf, gas_price_buf, step_buf, prepay_buf]);

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
    console.log("gid: " + gid.toString(16))
    return {
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

function call_contract(str_prikey, input, amount, key, value) {
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
        0,
        key,
        value);
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
            console.log(chunk);
        })
    });

    post_req.write(post_data);
    post_req.end();
}

function sendHttpRequest(path, in_data, encoding = 'utf8') {
    var post_data = querystring.stringify(in_data);
    let options = {
        host: '127.0.0.1',
        port: '23001',
        path: path,
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'Content-Length': Buffer.byteLength(post_data)
        }
    };
    
    let data = "";
    return new Promise(function (resolve, reject) {
        let req = http.request(options, function(res) {
            res.setEncoding(encoding);
            res.on('data', function(chunk) {
                data += chunk;
            });
 
            res.on('end', function() {
                resolve({result: true, data: data});
            });
        });
 
        req.write(post_data);
        req.on('error', (e) => {
            resolve({result: false, errmsg: e.message});
        });
        req.end();
    });
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
    var data = create_tx(str_prikey, contract_address, 0, 100000, 1, prepay, 7, "key", "value");
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
    var cmd = `clickhouse-client --host 127.0.0.1 --port 9000 -q "select count(distinct(user)) from zjc_ck_prepayment_table where contract='${contract_address}' and user in (${check_accounts_str});"`;
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

            console.log(`${cmd} contract prepayment failed error: ${stderr} count: ${stdout}`);
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
            var cons_codes = "";
            {
                var key_len = key.length.toString();
                if (key.length <= 9) {
                    key_len = "0" + key_len;
                }
    
                var param = "readd" + key_len + key + value;
                var hexparam = web3.utils.toHex(param);
                // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
                cons_codes = web3.eth.abi.encodeParameters(
                    ['bytes'], 
                    [hexparam]);
                console.log("cons_codes: " + cons_codes.substring(2));
            }
            // 转账到管理账户，创建合约
            {
                new_contract(
                    "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
                    out_lines[3] + cons_codes.substring(2));
                var contract_cmd = `clickhouse-client --host 127.0.0.1 --port 9000 -q  "SELECT to FROM zjc_ck_account_key_value_table where type = 6 and key in ('5f5f6b437265617465436f6e74726163744279746573436f6465',  '5f5f6b437265617465436f6e74726163744279746573436f6465') and to='${contract_address}' limit 1;"`
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

                // 预设值合约调用币，并等待成功
                SetManagerPrepayment(contract_address);
            }
        }
      });
}

function CreatePrivateAndPublicKeys(id, content) {
    var key = "tpinit"
    var value = id+";29eb86b7292ce572e46c6bdf8d8639dc6918991b,5b88d2cc46b94f199d12ff3c050c2db337871051,792e703d820ee43e5014803297208e7a774ceaa5,713b6605d93f07badf314cafe941ba2c4c6ad4bc,52a888b6437f8ebde99ec8929ff3f0f1fa533ad5,01a9a0ad9d65bcf47bb3326da7fd2aef8c18ea88,55c6b8dd50c1430969022788787d972848158c46,19b65ddc3cafa7942e5ac54394f09f1990ca2ac7,19d981de4f615ab95e61553421c358b96e997a2c,7bdc787d0aaddf760eab83de971028712123f1ca";
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    hex_content = web3.utils.toHex(content);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes32', 'bytes', 'bytes'], 
        ['0x' + id, '0x' + gid, hex_content, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('CreatePrivateAndPublicKeys(bytes32,bytes32,bytes,bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0, web3.utils.toHex("def"), content);
}

function CreateReEncryptionKeys(id, content) {
    var key = "tprenk"
    var value = id+";468a50340bc016c5161df8a40dd7890a84132750,204be12c7d5a77f4cecca96aeb1aadd0dc11e829,4d84890349d26fa23fb94ee32d16a4a522025072,4855a79cbcfc3d54cd99d504872beee01c8f9096,543cf9eec69613a4a01c28ebe64e50fbe234b57f,026047a338ee52e99f611bd02e9e5b12ecb83b74,35c95955d7bca26ccea47b3bf1aacc5936b53c5a,5e1694b4226bd0e1b75b71a249d6126c87d14a9e,48d9915add7e5bf58e5ad8adef850fb899c3d6ea,166ab66ee71d78d357c2c69697267c30fb820c65\n578997ffbf36d620eabeed6c6462090bf4850063,1ca9d6839beec8a8ae44aa717b217ea49929333d,6f20963912e7b78aba3a27850a53e2d2898d156d\n77f1868888fea3dc4cf479b9fed840c50e7b80e6,1c4bdf20371da32c30499e0e46438dcd0f829c21,3029b3818313522b65700c0bfaffe8741acfd1de\n4544042ac99c72c0740f78945b3842d90e362da3ce950cd40daa73da7651ab2908318301af471419fbd37334b8514a7031f70761bbf5b3755c4221cf7147ec1153066cef10869dff1ab7f6d39043dbc8287f06f6588e9418b3f253e501ae3881daf1a54363a37c5f4a3a9ba8b4a5cdfeea0993aef11fb0187ebb57a18eec290a,3b5987fc4a0a333f36376062f4b917560901e80e765b908a982bb33d276eb8fdacbfeffb63baefb81559c032d47e0afc1c4d70c4785cef138027f296c4585b17500b95dcb5a1e67458dbefb741f1ecd9fb100e50bd6ece1fe23714238d468a92eafaaa1c7ac6d2f6e722480b5729eff536c6e5d2262c5be3cc4f2c469fa8e2ea,8b94d276adea01a630b195216a210bdda9e4a07929d3a3c3d5660bc3a9726f75d2ba89bf64089b505562ac0c3a6f81e28936ddd235eaf185bd63c6e7ccd459187094a3f5e2c2a2b36b01ba8f6a072194cb276f3700440c972aa6b194c150a2a5e0c72f1df1689a01f60b42c270e2bfbbf6ced7edfd677816aadb4d8a5da6e72a,849ff0d2ffabf448864463b78d7de061e0142cbe12131b7f944c6df368c8d3223067e68f1f555b995c84320466b43f4dbef933ef201b433cd97972f371113f3a4adedaaec087673ab079bf217e3ee54bf7fa03435addb2262a99f04ef203b99108bcee8555189b543fb353998345e8f20b9f1a25f92d0ef501338ed28dbb27f0,3850f3d6b6e8305c7bf536e024843d8f9170660e514bae7342152aa14e07eb3b75f462d6226c34e00398e48bfd9265a07684a9ddb5bba7c8010a029f25784b992a0559ec761f0d72728ad37df2c067cff8c0b1f33516cd1b5f2a01def52abd782f114e545725dc8a79be4453552c7b99b8c71298661d6a2e17e0e7858f2c4d51,1e8b6f9ecc6c317d51923ff1a730365237115c99cb573f19241e7ae3938a817df95d45ccd7fcbea2d87c97b2a1be265e43a5a270913d1c14dfa19866ccd55bc04889db7f30cb04f210c73520ee72175b7c26cf9324a55755e5f4dc6eab57b9d0c8a9ee538adf84880dc0f5d181fb15f352cade261cf4a6c0376e4144399275c6,7a669e8d8a9cbb185626b0af4eb467cb86ee1e5393165544715e7486198394319e1b8790335ff5949d17a4054974e84e703c85b7bbfc1e577e2feeaaeaf16acb83ba19a36403aab7f796788fa31546a72e5b7b4d419b49a1fa63757791390d3327debb38e7b6fcc80df28c67c119368b57e989bb1bc7ea667b642f1f71b7181b,a008e91fc6085a0386b0076edc3796a2a0c431e5f946556ae63c9a173a47fe08eae37b9fce61c05138c4a04de7fe024afdacc556f9d0fbb5020c03ae8b5173de4b4933b9ce0c611bd0ce6eb6b68947e12b5c7a4e93febc1b880c6e9e917aeebd9da5e3f9251f83150d2b89654f8631ed0e7f53ccc67dbfc2cb495747391ca550,4749b5b7cfb6c8e9f7418615a53d94b582cc873a8e586ac06837885f22d44bdca39ff8109b19372481b7c58cc7dd91958abd20b26eaffb38006c8c1874714e2615403ad7b65ebd01c98be53382f77c25c69a8fd2e0ca490271a77ee2118bd541223a73b45b9ea26109fb5274360aa276437271c1a9a78e5b026ed63aec01146d\n4eb24680d02ab2ba45ef78e8b425f7c94eb662d0,3545ad3b3fe0c2f4c08a900eae3d03c87969ba6c,336293b77152b9e4d718481c6153c2d4369561b5,578e3eb7048153b05577276a0b1c1059140be7c9,604e8223208efb81daa9778237fa75fc22344a1c,138eb8b0d8c6073a080179935b8b9b92d00c589b,0f2ea3cf55743836fbbf9a5756a524be3adb1c78,247922d501351bfea6ab14bab95cf0cf912ef84e,78cddbd9d4299c953cfa839feac915e0bcdf7bce";
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    hex_content = web3.utils.toHex(content);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes32', 'bytes',  'bytes'], 
        ['0x' + id, '0x' + gid, hex_content, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('CreateReEncryptionKeys(bytes32,bytes32,bytes,bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0, web3.utils.toHex("def"), content);
}

function EncryptUserMessage(id, content) {
    var key = "tpencu"
    var value = id+";";
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    hex_content = web3.utils.toHex(content);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes32', 'bytes', 'bytes'], 
        ['0x' + id, '0x' + gid, hex_content, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('EncryptUserMessage(bytes32,bytes32,bytes,bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0, web3.utils.toHex("def"), content);
}

function ReEncryptUserMessage(id, content) {
    var key = "tprenc"
    var value = id+";";
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    hex_content = web3.utils.toHex(content);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes32', 'bytes',  'bytes'], 
        ['0x' + id, '0x' + gid, hex_content, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('ReEncryptUserMessage(bytes32,bytes32,bytes,bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0, web3.utils.toHex("def"), content);
}

function ReEncryptUserMessageWithMember(id, index, content) {
    var key = "mprenc"
    var value = id+";"+index;
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    hex_content = web3.utils.toHex(content);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes32', 'bytes',  'bytes'], 
        ['0x' + id, '0x' + gid, hex_content, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('ReEncryptUserMessageWithMember(bytes32,bytes32,bytes,bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0,web3.utils.toHex("def"), content);
}

function Decryption(id, content) {
    var key = "tprdec"
    var value = id+";";
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    hex_content = web3.utils.toHex(content);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32', 'bytes32', 'bytes',  'bytes'], 
        ['0x' + id, '0x' + gid, hex_content, hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('Decryption(bytes32,bytes32,bytes,bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    call_contract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2), 0, web3.utils.toHex("def"), content);
}

function GetAllProxyJson() {
    var addParamCode = web3.eth.abi.encodeFunctionSignature('GetAllProxyJson()');
    console.log("GetAllProxyJson 0: " + addParamCode.substring(2));
    QueryContract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2));
}

function GetAllGidJson(id) {
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes32'], 
        ['0x' + id]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('GetAllGidJson(bytes32)');
    console.log("GetAllGidJson 0: " + addParamCode.substring(2) + addParam.substring(2));
    QueryContract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2));
}

function JustCallRipemd160(id) {
    var key = "tprdec"
    var value = id+";";
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = key + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    // var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    var addParam = web3.eth.abi.encodeParameters(
        ['bytes'], 
        [hexparam]);
    var addParamCode = web3.eth.abi.encodeFunctionSignature('JustCallRipemd160(bytes)');
    console.log("addParam 0: " + key + ":" + value + "," + addParamCode.substring(2) + addParam.substring(2));
    QueryContract(
        "cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848", 
        addParamCode.substring(2) + addParam.substring(2));
}

const args = process.argv.slice(2)
var tmp_id = args[1]
var id = keccak256('7540498158068831994142082110286533992664756308802229570786251794' + contract_address + tmp_id).toString('hex');
// SetUp：初始化算法，需要用到pbc库
if (args[0] == 0) {
    var pairing_param_value = "type a\nq 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\nh 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\nr 730750818665451621361119245571504901405976559617\nexp2 159\nexp1 107\nsign1 1\nsign0 1";
    InitC2cEnv("c0", pairing_param_value);
}

// CreatPath(i)：由用户i选择多个被委托者。按选择顺序生成一个路径（列表），其中存放被委托者的公钥。
if (args[0] == 1) {
    CreatePrivateAndPublicKeys(id, args[2]);
    console.log(id)
}

// RKGen：重加密密钥生成，需要用到pbc库
if (args[0] == 2) {
    CreateReEncryptionKeys(id, args[2]);
}

// Upd：token更新算法，需要用到pbc库
if (args[0] == 3) {
    // add_pairing_param("tpencu", "tpencu", id+";tpencu");
    EncryptUserMessage(id, args[2]);
}

// ReEnc：重加密，需要用到pbc库 (这一步包含一个分布式随机数生成协议，即多个代理协商出一个统一的随机数)
if (args[0] == 4) {
    // add_pairing_param("tprenc", "tprenc", id+";tprenc");
    ReEncryptUserMessage(id, args[2]);
}

// ReEnc：重加密，需要用到pbc库 (这一步包含一个分布式随机数生成协议，即多个代理协商出一个统一的随机数)
if (args[0] == 5) {
    // add_pairing_param("mprenc", "mprenc", id+";"+args[1]);
    ReEncryptUserMessageWithMember(id, args[2], args[3]);
}

// dec
if (args[0] == 6) {
    // add_pairing_param("tprdec", "tprdec", id+";tprdec");
    Decryption(id, args[2]);
}

// 测试合约查询
if (args[0] == 30) {
    GetAllGidJson(id);
    JustCallRipemd160(id, "test");
}
