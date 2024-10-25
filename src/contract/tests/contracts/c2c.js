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
    //const privateKeyBuf = Secp256k1.uint256("20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 16) // e252d01a37b85e2007ed3cc13797aa92496204a4
    //const privateKeyBuf = Secp256k1.uint256("10ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 16) // b5f4bf70ae9afb2649e47488d8cd1574eef2c693 
    const privateKeyBuf = Secp256k1.uint256("863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 16) // e10fe8543f02ca7739803df692b8122cd200c9d7 
    //const privateKeyBuf = Secp256k1.uint256("d5a4758b94d34da11f818efbbc7b6739949aa7cb249c9403022b4ed54fa7b0a8", 16)
    self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
    self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
    var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
    var address = keccak256(pk_bytes).toString('hex')
    address = address.slice(address.length - 40, address.length)
    console.log("self_account_id: " + address.toString('hex'));
    self_account_id = address;
    contract_address = fs.readFileSync('contract_address', 'utf-8');
    console.log("contract_address: " + contract_address);
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

function create_tx(to, amount, gas_limit, gas_price, prepay, tx_type) {
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
        amount_buf, gas_limit_buf, gas_price_buf, step_buf, prepay_buf]);
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
        'pepay': prepay
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
        0);
    PostCode(data);

    const opt = { flag: 'w', }
    fs.writeFile('contract_address', self_contract_address, opt, (err) => {
        if (err) {
            console.error(err)
        }
    })

    return self_contract_address;
}

function call_contract(input, amount) {
    contract_address = fs.readFileSync('contract_address', 'utf-8');
    console.log("contract_address: " + contract_address);
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = param_contract(
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

function do_transaction(to_addr, amount, gas_limit, gas_price) {
    var data = create_tx(to_addr, amount, gas_limit, gas_price, 0, 0);
    PostCode(data);
}

function CreatePhr() {
    console.log("test smart contract signature: ");
    var account1 = web3.eth.accounts.privateKeyToAccount('0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
    console.log("account1 :");
    console.log(account1.address);
    var account2 = web3.eth.accounts.privateKeyToAccount('0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
    console.log("account2 :");
    console.log(account2.address);
    var account3 = web3.eth.accounts.privateKeyToAccount('0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
    console.log("account3 :");
    console.log(account3.address);

    var cons_codes = web3.eth.abi.encodeParameters(['address[]', 'uint256', 'uint256'],
        [[account1.address,
        account2.address,
            account3.address], 10000000000, 100000000]);
    console.log("cons_codes: " + cons_codes.substring(2));

    {
        var func = web3.eth.abi.encodeFunctionSignature('NewSellOrder(bytes,uint256)');
        var funcParam = web3.eth.abi.encodeParameters(['bytes', 'uint256'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', 1]);
        console.log("NewSellOrder func: " + func.substring(2) + funcParam.substring(2));
    }
    
    {
        var func = web3.eth.abi.encodeFunctionSignature('Confirm(address,uint256)');
        var funcParam = web3.eth.abi.encodeParameters(['address', 'uint256'], [account1.address, 1000000000]);
        console.log("Confirm func: " + func.substring(2) + funcParam.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('GetOrdersJson()');
        console.log("GetOrdersJson func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('SellerRelease()');
        console.log("SellerRelease func: " + func.substring(2));
    }

    {
        var func = web3.eth.abi.encodeFunctionSignature('ManagerRelease(address)');
        var funcParam = web3.eth.abi.encodeParameters(['address'], [account1.address]);
        console.log("Confirm func: " + func.substring(2) + funcParam.substring(2));
    }
   
    {
        var func = web3.eth.abi.encodeFunctionSignature('TestContract(uint256)');
        var funcParam = web3.eth.abi.encodeParameters(['uint256'], [1]);
        console.log("TestContract func: " + func.substring(2) + funcParam.substring(2));
    }

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

function QueryContract(input) {
    var contract_address = fs.readFileSync('contract_address', 'utf-8');
    var data = {
        "input": input,
        'address': contract_address,
        'from': self_account_id,
    };

    QueryPostCode('/query_contract', data);
}

function Prepayment(prepay) {
    var contract_address = fs.readFileSync('contract_address', 'utf-8');
    var data = create_tx(contract_address, 0, 100000, 1, prepay, 7);
    PostCode(data);
}

async function SetManagerPrepayment(contract_address) {
    // 为管理账户设置prepayment
    {
        const privateKeyBuf = Secp256k1.uint256("20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5", 16)
        self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
        self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
        var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
        var address = keccak256(pk_bytes).toString('hex')
        address = address.slice(address.length - 40, address.length)
        console.log("self_account_id: " + address.toString('hex'));
        self_account_id = address;
        Prepayment(1000000000000);
    }

    {
        const privateKeyBuf = Secp256k1.uint256("748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05", 16)
        self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
        self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
        var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
        var address = keccak256(pk_bytes).toString('hex')
        address = address.slice(address.length - 40, address.length)
        console.log("self_account_id: " + address.toString('hex'));
        self_account_id = address;
        Prepayment(1000000000000);
    }

    {
        const privateKeyBuf = Secp256k1.uint256("b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5", 16)
        self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
        self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
        var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
        var address = keccak256(pk_bytes).toString('hex')
        address = address.slice(address.length - 40, address.length)
        console.log("self_account_id: " + address.toString('hex'));
        self_account_id = address;
        Prepayment(1000000000000);
    }

    {
        const privateKeyBuf = Secp256k1.uint256("b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e0", 16)
        self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
        self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
        var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
        var address = keccak256(pk_bytes).toString('hex')
        address = address.slice(address.length - 40, address.length)
        console.log("self_account_id: " + address.toString('hex'));
        self_account_id = address;
        Prepayment(1000000000000);
    }

    var account1 = web3.eth.accounts.privateKeyToAccount(
        '0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5');
    var account2 = web3.eth.accounts.privateKeyToAccount(
        '0x748f7eaad8be6841490a134e0518dafdf67714a73d1275f917475abeb504dc05');
    var account3 = web3.eth.accounts.privateKeyToAccount(
        '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e5');
    var account4 = web3.eth.accounts.privateKeyToAccount(
        '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e0');
    var cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q "select count(distinct(user)) from zjc_ck_prepayment_table where contract='${contract_address}' and user in ('${account1.address.toString('hex').toLowerCase().substring(2)}', '${account2.address.toString('hex').toLowerCase().substring(2)}', '${account3.address.toString('hex').toLowerCase().substring(2)}', '${account4.address.toString('hex').toLowerCase().substring(2)}');"`;
    const { exec } = require('child_process');
    const execPromise = util.promisify(exec);
    // 检查合约是否创建成功
    var try_times = 0;
    while (try_times < 30) {
        try {
            const {stdout, stderr} = await execPromise(cmd);
            if (stdout.trim() == "4") {
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

function InitC2cEnv() {
    const { exec } = require('child_process');
    exec('solc --bin ./c2c.sol', async (error, stdout, stderr) => {
        if (error) {
          console.error(`exec error: ${error}`);
          return;
        }

        var out_lines = stdout.split('\n');
        console.log(`solc bin codes: ${out_lines[3]}`);
        {
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

            var seller_accounts = new Set();
            for (var i = 10; i < 30; ++i) {
                // 卖家账户设置
                var account4 = web3.eth.accounts.privateKeyToAccount(
                    '0xb546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1' + str(i));
                console.log(`account ${i}:`);
                console.log(account4.address);
                do_transaction(account4.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                seller_accounts.add(account4.address.toString('hex').toLowerCase().substring(2));
            }
           
            var cons_codes = web3.eth.abi.encodeParameters(['address[]', 'uint256', 'uint256'],
                [[account1.address,
                account2.address,
                    account3.address], 10000000000, 100000000]);
            console.log("cons_codes: " + cons_codes.substring(2));
            // 转账到管理账户，创建合约
            {
                const privateKeyBuf = Secp256k1.uint256("863cc3200dd93e1743f63c49f1bd3d19d0f4cba330dbba53e69706cc671a568f", 16)
                self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
                self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
                var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
                var address = keccak256(pk_bytes).toString('hex')
                address = address.slice(address.length - 40, address.length)
                console.log("self_account_id: " + address.toString('hex'));
                self_account_id = address; 
                
                do_transaction(account1.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                do_transaction(account2.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                do_transaction(account3.address.toString('hex').toLowerCase().substring(2), 1100000000000, 100000, 1);
                var contract_address = new_contract(out_lines[3] + cons_codes.substring(2));
                var contract_cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q "SELECT to FROM zjc_ck_account_key_value_table where type = 6 and key in('5f5f6b437265617465436f6e74726163744279746573436f6465', '5f5f6b437265617465436f6e74726163744279746573436f6465') and to='${contract_address}' limit 1;"`
                var try_times = 0;
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

                var cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q "select id, balance from zjc_ck_account_table where id in 
                    ('${account1.address.toString('hex').toLowerCase().substring(2)}', 
                    '${account2.address.toString('hex').toLowerCase().substring(2)}', 
                    '${account3.address.toString('hex').toLowerCase().substring(2)}', 
                    '${account4.address.toString('hex').toLowerCase().substring(2)}');"`;
                // 检查合约是否创建成功
                var try_times = 0;
                while (try_times < 30) {
                    try {
                    // wait for exec to complete
                        const {stdout, stderr} = await execPromise(cmd);
                        var split_lines = stdout.trim().split('\n');
                        var dictionary = new Set();
                        console.log(`transfer to manager address split_lines.length: ${split_lines.length}`);
                        if (split_lines.length >= 4) {
                            for (var line_idx = 0; line_idx < split_lines.length; ++line_idx) {
                                var item_split = split_lines[line_idx].split("\t");
                                var new_balance = parseInt(item_split[1].trim(), 10);
                                if (new_balance >= 1100000000000) {
                                    dictionary.add(item_split[0].trim());
                                    console.log(`transfer to manager address new_balance: ${new_balance} dictionary size ${dictionary.size}`);
                                }
                            }
                        }

                        if (dictionary.size == 4) {
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

                SetManagerPrepayment(contract_address);
            }
        }
      });
}

async function CreateNewSeller() {
    const { exec } = require('child_process');
    const execPromise = util.promisify(exec);

    {
        const privateKeyBuf = Secp256k1.uint256("b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e0", 16)
        self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
        self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
        var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
        var address = keccak256(pk_bytes).toString('hex')
        address = address.slice(address.length - 40, address.length)
        console.log("self_account_id: " + address.toString('hex'));
        self_account_id = address;
    }

    var old_prepayment = 0;
    {
        var contract_address = fs.readFileSync('contract_address', 'utf-8');
        var cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q "select prepayment from zjc_ck_prepayment_table where contract='${contract_address}' and user='${self_account_id}' order by height desc limit 1;"`;
        // 检查合约是否创建成功
        var try_times = 0;
        while (try_times < 30) {
            try {
            // wait for exec to complete
                const {stdout, stderr} = await execPromise(cmd);
                if (stdout.trim() != "") {
                    console.error(`get old prepayment success: ${stdout}`);
                    old_prepayment = parseInt(stdout, 10)
                    break;
                }

                console.log(`get old prepayment error: ${stderr} count: ${stdout}`);
            } catch (error) {
                console.log(error);
            }

            ++try_times;
            await sleep(2000);
        }

        if (try_times >= 30) {
            console.error(`get old prepayment failed!`);
            return;
        }
    }

    var sell_amount = 10000000000;
    {
        var func = web3.eth.abi.encodeFunctionSignature('NewSellOrder(bytes,uint256)');
        var funcParam = web3.eth.abi.encodeParameters(['bytes', 'uint256'], ['0x20ac5391ad70648f4ac6ee659e7709c0305c91c968c91b45018673ba5d1841e5', 1]);
        console.log("NewSellOrder func: " + func.substring(2) + funcParam.substring(2));
        call_contract(func.substring(2) + funcParam.substring(2), sell_amount);
    }

    {
        var contract_address = fs.readFileSync('contract_address', 'utf-8');
        var cmd = `clickhouse-client --host 82.156.224.174 --port 9000 -q "select prepayment from zjc_ck_prepayment_table where contract='${contract_address}' and user='${self_account_id}' order by height desc limit 1;"`;
        // 检查合约是否创建成功
        var try_times = 0;
        while (try_times < 30) {
            try {
            // wait for exec to complete
                const {stdout, stderr} = await execPromise(cmd);
                if (stdout.trim() != "") {
                    var new_prepayment = parseInt(stdout, 10)
                    if (old_prepayment - new_prepayment >= sell_amount) {
                        console.error(`get new prepayment success: ${stdout.trim()}, sell_amount: ${sell_amount}`);
                        break;
                    }

                    if (old_prepayment > new_prepayment) {
                        console.error(`get new prepayment failed: ${stdout.trim()}, sell_amount: ${sell_amount}`);
                        break;
                    }
                }

                console.log(`${cmd} get new prepayment error: ${stderr} count: ${stdout}`);
            } catch (error) {
                console.log(error);
            }

            ++try_times;
            await sleep(2000);
        }

        if (try_times >= 30) {
            console.error(`get new prepayment failed!`);
            return;
        }
    }

    QueryContract("cdfd45bb");
}

const args = process.argv.slice(2)
if (args[0] == 0) {
    InitC2cEnv();
    return;
}

// 创建卖单
if (args[0] == 1) {
    CreateNewSeller();
    return;
}

// 测试合约查询
if (args[0] == 8) {
    {
        const privateKeyBuf = Secp256k1.uint256("b546fd36d57b4c9adda29967cf6a1a3e3478f9a4892394e17225cfb6c0d1d1e0", 16)
        self_private_key = Secp256k1.uint256(privateKeyBuf, 16)
        self_public_key = Secp256k1.generatePublicKeyFromPrivateKeyData(self_private_key)
        var pk_bytes = hexToBytes(self_public_key.x.toString(16) + self_public_key.y.toString(16))
        var address = keccak256(pk_bytes).toString('hex')
        address = address.slice(address.length - 40, address.length)
        console.log("self_account_id: " + address.toString('hex'));
        self_account_id = address;
    }
    QueryContract("cdfd45bb");
}















init_private_key();
if (args[0] == 0) {
    do_transaction(args[1], args[2], 100000, 1);
}

if (args[0] == 1) {
    new_contract("608060405260405162005553380380620055538339818101604052810190620000299190620003a8565b60008351905060005b81811015620000c65760016006600087848151811062000057576200005662000423565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff0219169083151502179055508080620000bd9062000481565b91505062000032565b50600080819055506001600660003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff021916908315150217905550826002819055508160038190555033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050505050620004ce565b6000604051905090565b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b620001e38262000198565b810181811067ffffffffffffffff82111715620002055762000204620001a9565b5b80604052505050565b60006200021a6200017f565b9050620002288282620001d8565b919050565b600067ffffffffffffffff8211156200024b576200024a620001a9565b5b602082029050602081019050919050565b600080fd5b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b60006200028e8262000261565b9050919050565b620002a08162000281565b8114620002ac57600080fd5b50565b600081519050620002c08162000295565b92915050565b6000620002dd620002d7846200022d565b6200020e565b905080838252602082019050602084028301858111156200030357620003026200025c565b5b835b818110156200033057806200031b8882620002af565b84526020840193505060208101905062000305565b5050509392505050565b600082601f83011262000352576200035162000193565b5b815162000364848260208601620002c6565b91505092915050565b6000819050919050565b62000382816200036d565b81146200038e57600080fd5b50565b600081519050620003a28162000377565b92915050565b600080600060608486031215620003c457620003c362000189565b5b600084015167ffffffffffffffff811115620003e557620003e46200018e565b5b620003f3868287016200033a565b9350506020620004068682870162000391565b9250506040620004198682870162000391565b9150509250925092565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006200048e826200036d565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203620004c357620004c262000452565b5b600182019050919050565b61507580620004de6000396000f3fe6080604052600436106101145760003560e01c80638da5cb5b116100a0578063c77927c911610064578063c77927c91461036e578063c9a7ac0514610397578063cdfd45bb146103d4578063d789c2d1146103ff578063f40e84711461041b57610114565b80638da5cb5b146102b757806399efa4b3146102e2578063ae7abc801461030d578063b1276e6d14610329578063bde73c391461035257610114565b80634e64f75b116100e75780634e64f75b146101b957806351fdd191146101d5578063593b79fe1461020057806370587c101461023d578063856ed0941461027a57610114565b80630cdb0d53146101195780630f85011c146101565780632e1e5dbe146101605780632e559c0d1461017c575b600080fd5b34801561012557600080fd5b50610140600480360381019061013b9190613f93565b610463565b60405161014d919061405b565b60405180910390f35b61015e610669565b005b61017a600480360381019061017591906140db565b610cfb565b005b34801561018857600080fd5b506101a3600480360381019061019e919061413e565b61121a565b6040516101b0919061405b565b60405180910390f35b6101d360048036038101906101ce91906140db565b611277565b005b3480156101e157600080fd5b506101ea611a2b565b6040516101f7919061417a565b60405180910390f35b34801561020c57600080fd5b50610227600480360381019061022291906140db565b611a31565b604051610234919061405b565b60405180910390f35b34801561024957600080fd5b50610264600480360381019061025f919061427b565b611a5a565b604051610271919061405b565b60405180910390f35b34801561028657600080fd5b506102a1600480360381019061029c91906140db565b611be2565b6040516102ae91906142f2565b60405180910390f35b3480156102c357600080fd5b506102cc611c02565b6040516102d9919061431c565b60405180910390f35b3480156102ee57600080fd5b506102f7611c28565b604051610304919061417a565b60405180910390f35b61032760048036038101906103229190614337565b611c2e565b005b34801561033557600080fd5b50610350600480360381019061034b91906140db565b612238565b005b61036c600480360381019061036791906143d1565b612390565b005b34801561037a57600080fd5b50610395600480360381019061039091906144d4565b612d64565b005b3480156103a357600080fd5b506103be60048036038101906103b99190614691565b612eb3565b6040516103cb919061405b565b60405180910390f35b3480156103e057600080fd5b506103e96137f9565b6040516103f6919061405b565b60405180910390f35b6104196004803603810190610414919061413e565b613c06565b005b34801561042757600080fd5b50610442600480360381019061043d91906140db565b613c80565b60405161045a9c9b9a999897969594939291906146fc565b60405180910390f35b606060006002835161047591906147ec565b67ffffffffffffffff81111561048e5761048d613e68565b5b6040519080825280601f01601f1916602001820160405280156104c05781602001600182028036833780820191505090505b50905060006040518060400160405280601081526020017f3031323334353637383961626364656600000000000000000000000000000000815250905060005b845181101561065e5781825186838151811061051f5761051e61482e565b5b602001015160f81c60f81b60f81c60ff1661053a919061488c565b8151811061054b5761054a61482e565b5b602001015160f81c60f81b8360028361056491906147ec565b815181106105755761057461482e565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a9053508182518683815181106105ba576105b961482e565b5b602001015160f81c60f81b60f81c60ff166105d591906148bd565b815181106105e6576105e561482e565b5b602001015160f81c60f81b83600160028461060191906147ec565b61060b91906148ee565b8151811061061c5761061b61482e565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a905350808061065690614922565b915050610500565b508192505050919050565b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff166106c257600080fd5b6000600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206040518061018001604052908160008201805461071f90614999565b80601f016020809104026020016040519081016040528092919081815260200182805461074b90614999565b80156107985780601f1061076d57610100808354040283529160200191610798565b820191906000526020600020905b81548152906001019060200180831161077b57829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152602001600682015481526020016007820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001600882015481525050905060018160a00190151590811515815250504381610120018181525050806080015115610b5a573373ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f1935050505015801561095d573d6000803e3d6000fd5b506000600780549050905060005b81811015610a3b573373ffffffffffffffffffffffffffffffffffffffff166007828154811061099e5761099d61482e565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603610a2a57600781815481106109f8576109f761482e565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055610a3b565b80610a3490614922565b905061096b565b50600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008082016000610a8c9190613ddc565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff0219169055600582016000905560068201600090556007820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff02191690556008820160009055505050610cf8565b80600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000820151816000019081610baf9190614b76565b5060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550604082015181600201556060820151816003015560808201518160040160006101000a81548160ff02191690831515021790555060a08201518160040160016101000a81548160ff02191690831515021790555060c08201518160040160026101000a81548160ff02191690831515021790555060e08201518160040160036101000a81548160ff021916908315150217905550610100820151816005015561012082015181600601556101408201518160070160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061016082015181600801559050505b50565b600560008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff16610d5457600080fd5b600660003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff16610daa57600080fd5b6000600560008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060405180610180016040529081600082018054610e0790614999565b80601f0160208091040260200160405190810160405280929190818152602001828054610e3390614999565b8015610e805780601f10610e5557610100808354040283529160200191610e80565b820191906000526020600020905b815481529060010190602001808311610e6357829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152602001600682015481526020016007820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160088201548152505090508173ffffffffffffffffffffffffffffffffffffffff16816020015173ffffffffffffffffffffffffffffffffffffffff161461101157600080fd5b806080015161101f57600080fd5b6000600780549050905060005b818110156110fc578373ffffffffffffffffffffffffffffffffffffffff166007828154811061105f5761105e61482e565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16036110eb57600781815481106110b9576110b861482e565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff02191690556110fc565b806110f590614922565b905061102c565b50600560008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000808201600061114d9190613ddc565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff0219169055600582016000905560068201600090556007820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff021916905560088201600090555050505050565b6060602067ffffffffffffffff81111561123757611236613e68565b5b6040519080825280601f01601f1916602001820160405280156112695781602001600182028036833780820191505090505b509050816020820152919050565b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3560016040516112a79190614c83565b60405180910390a1600560008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1661130857600080fd5b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3560026040516113389190614cd9565b60405180910390a1600660003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff1661139657600080fd5b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3560036040516113c69190614d2f565b60405180910390a16000600560008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206040518061018001604052908160008201805461142b90614999565b80601f016020809104026020016040519081016040528092919081815260200182805461145790614999565b80156114a45780601f10611479576101008083540402835291602001916114a4565b820191906000526020600020905b81548152906001019060200180831161148757829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152602001600682015481526020016007820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160088201548152505090507f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3560046040516116299190614d85565b60405180910390a18173ffffffffffffffffffffffffffffffffffffffff16816020015173ffffffffffffffffffffffffffffffffffffffff161461166d57600080fd5b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e35600560405161169d9190614ddb565b60405180910390a18060800151156116b457600080fd5b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3560066040516116e49190614e31565b60405180910390a16001816080019015159081151581525050438161012001818152505060008160400151111561181a577f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3547604051611744919061417a565b60405180910390a17f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e35816040015160405161177f919061417a565b60405180910390a1806020015173ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f193505050501580156117d5573d6000803e3d6000fd5b5060008160400181815250507f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3560076040516118119190614e87565b60405180910390a15b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e35600860405161184a9190614edd565b60405180910390a180600560008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008201518160000190816118a79190614b76565b5060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550604082015181600201556060820151816003015560808201518160040160006101000a81548160ff02191690831515021790555060a08201518160040160016101000a81548160ff02191690831515021790555060c08201518160040160026101000a81548160ff02191690831515021790555060e08201518160040160036101000a81548160ff021916908315150217905550610100820151816005015561012082015181600601556101408201518160070160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061016082015181600801559050507f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e356009604051611a1f9190614f33565b60405180910390a15050565b60035481565b606081604051602001611a449190614f96565b6040516020818303038152906040529050919050565b60606000805b83811015611aa457848181518110611a7b57611a7a61482e565b5b60200260200101515182611a8f91906148ee565b91508080611a9c90614922565b915050611a60565b5060008167ffffffffffffffff811115611ac157611ac0613e68565b5b6040519080825280601f01601f191660200182016040528015611af35781602001600182028036833780820191505090505b5090506000805b85811015611bd55760005b878281518110611b1857611b1761482e565b5b602002602001015151811015611bc157878281518110611b3b57611b3a61482e565b5b60200260200101518181518110611b5557611b5461482e565b5b602001015160f81c60f81b848480611b6c90614922565b955081518110611b7f57611b7e61482e565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a9053508080611bb990614922565b915050611b05565b508080611bcd90614922565b915050611afa565b5081935050505092915050565b60066020528060005260406000206000915054906101000a900460ff1681565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60025481565b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e3534604051611c5d919061417a565b60405180910390a1600254341015611c7457600080fd5b7f736cb082547b539f0f1abf8f2215512cfa58045e38d6f3d91cdc453b4c54a73633838334600054604051611cad959493929190614fb1565b60405180910390a1600660003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff1615611d0c57600080fd5b7f736cb082547b539f0f1abf8f2215512cfa58045e38d6f3d91cdc453b4c54a73633838334600054604051611d45959493929190614fb1565b60405180910390a1600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1615611f1457600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160009054906101000a900460ff16611dfb57600080fd5b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008082016000611e4b9190613ddc565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff0219169055600582016000905560068201600090556007820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600882016000905550505b6040518061018001604052808381526020013373ffffffffffffffffffffffffffffffffffffffff16815260200134815260200182815260200160001515815260200160001515815260200160011515815260200160001515815260200160005481526020014381526020013373ffffffffffffffffffffffffffffffffffffffff1681526020016000815250600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000820151816000019081611ff59190614b76565b5060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550604082015181600201556060820151816003015560808201518160040160006101000a81548160ff02191690831515021790555060a08201518160040160016101000a81548160ff02191690831515021790555060c08201518160040160026101000a81548160ff02191690831515021790555060e08201518160040160036101000a81548160ff021916908315150217905550610100820151816005015561012082015181600601556101408201518160070160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061016082015181600801559050506007339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055507f08ba3ca4e75f0e16c545e66735620d9a13e7303d575373d267e353a7f37234456007805490506040516121d4919061417a565b60405180910390a17f736cb082547b539f0f1abf8f2215512cfa58045e38d6f3d91cdc453b4c54a73633838334600054604051612215959493929190614fb1565b60405180910390a160008081548092919061222f90614922565b91905055505050565b600560008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1661229157600080fd5b600560008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160039054906101000a900460ff16156122eb57600080fd5b6001600560008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160036101000a81548160ff02191690831515021790555043600560008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206006018190555050565b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e35816040516123bf919061417a565b60405180910390a17f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e356003546040516123f8919061417a565b60405180910390a160035481101561240f57600080fd5b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1661246857600080fd5b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160009054906101000a900460ff16156124c257600080fd5b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160019054906101000a900460ff161561251c57600080fd5b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160039054906101000a900460ff161561257657600080fd5b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e35600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600201546040516125e7919061417a565b60405180910390a180600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020154101561263e57600080fd5b6000600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206040518061018001604052908160008201805461269b90614999565b80601f01602080910402602001604051908101604052809291908181526020018280546126c790614999565b80156127145780601f106126e957610100808354040283529160200191612714565b820191906000526020600020905b8154815290600101906020018083116126f757829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152602001600682015481526020016007820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020016008820154815250509050818160400181815161287b919061500b565b9150818152505043816101200181815250508281610140019073ffffffffffffffffffffffffffffffffffffffff16908173ffffffffffffffffffffffffffffffffffffffff168152505081816101600181815250508273ffffffffffffffffffffffffffffffffffffffff166108fc839081150290604051600060405180830381858888f19350505050158015612917573d6000803e3d6000fd5b5060035481604001511015612b865760008160400151111561297f573373ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f1935050505015801561297d573d6000803e3d6000fd5b505b60008160400181815250506000600780549050905060005b81811015612a67573373ffffffffffffffffffffffffffffffffffffffff16600782815481106129ca576129c961482e565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603612a565760078181548110612a2457612a2361482e565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055612a67565b80612a6090614922565b9050612997565b50600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008082016000612ab89190613ddc565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff0219169055600582016000905560068201600090556007820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff02191690556008820160009055505050612d24565b80600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000820151816000019081612bdb9190614b76565b5060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550604082015181600201556060820151816003015560808201518160040160006101000a81548160ff02191690831515021790555060a08201518160040160016101000a81548160ff02191690831515021790555060c08201518160040160026101000a81548160ff02191690831515021790555060e08201518160040160036101000a81548160ff021916908315150217905550610100820151816005015561012082015181600601556101408201518160070160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061016082015181600801559050505b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e358160400151604051612d57919061417a565b60405180910390a1505050565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614612dbe57600080fd5b600560003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1615612e1857600080fd5b60008151905060005b81811015612eae57600160066000858481518110612e4257612e4161482e565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff0219169083151502179055508080612ea690614922565b915050612e21565b505050565b60606000606467ffffffffffffffff811115612ed257612ed1613e68565b5b604051908082528060200260200182016040528015612f0557816020015b6060815260200190600190039081612ef05790505b50905060006040518060400160405280600681526020017f7b2272223a220000000000000000000000000000000000000000000000000000815250828280612f4c90614922565b935081518110612f5f57612f5e61482e565b5b6020026020010181905250612f778560000151610463565b828280612f8390614922565b935081518110612f9657612f9561482e565b5b60200260200101819052506040518060400160405280600781526020017f222c2261223a2200000000000000000000000000000000000000000000000000815250828280612fe390614922565b935081518110612ff657612ff561482e565b5b60200260200101819052506130166130118660200151611a31565b610463565b82828061302290614922565b9350815181106130355761303461482e565b5b60200260200101819052506040518060400160405280600781526020017f222c2262223a220000000000000000000000000000000000000000000000000081525082828061308290614922565b9350815181106130955761309461482e565b5b60200260200101819052506130b66130b1866101400151611a31565b610463565b8282806130c290614922565b9350815181106130d5576130d461482e565b5b60200260200101819052506040518060400160405280600781526020017f222c226d223a220000000000000000000000000000000000000000000000000081525082828061312290614922565b9350815181106131355761313461482e565b5b6020026020010181905250613155613150866040015161121a565b610463565b82828061316190614922565b9350815181106131745761317361482e565b5b60200260200101819052506040518060400160405280600781526020017f222c2270223a22000000000000000000000000000000000000000000000000008152508282806131c190614922565b9350815181106131d4576131d361482e565b5b60200260200101819052506131f46131ef866060015161121a565b610463565b82828061320090614922565b9350815181106132135761321261482e565b5b60200260200101819052506040518060400160405280600781526020017f222c2268223a220000000000000000000000000000000000000000000000000081525082828061326090614922565b9350815181106132735761327261482e565b5b602002602001018190525061329461328f86610120015161121a565b610463565b8282806132a090614922565b9350815181106132b3576132b261482e565b5b60200260200101819052506040518060400160405280600881526020017f222c22626d223a2200000000000000000000000000000000000000000000000081525082828061330090614922565b9350815181106133135761331261482e565b5b602002602001018190525061333461332f86610160015161121a565b610463565b82828061334090614922565b9350815181106133535761335261482e565b5b602002602001018190525060006040518060400160405280600581526020017f66616c736500000000000000000000000000000000000000000000000000000081525090508560800151156133db576040518060400160405280600481526020017f747275650000000000000000000000000000000000000000000000000000000081525090505b60006040518060400160405280600581526020017f66616c736500000000000000000000000000000000000000000000000000000081525090508660a0015115613458576040518060400160405280600481526020017f747275650000000000000000000000000000000000000000000000000000000081525090505b60006040518060400160405280600581526020017f66616c736500000000000000000000000000000000000000000000000000000081525090508760e00151156134d5576040518060400160405280600481526020017f747275650000000000000000000000000000000000000000000000000000000081525090505b6040518060400160405280600781526020017f222c226d72223a0000000000000000000000000000000000000000000000000081525085858061351790614922565b96508151811061352a5761352961482e565b5b60200260200101819052508285858061354290614922565b9650815181106135555761355461482e565b5b60200260200101819052506040518060400160405280600681526020017f2c227372223a00000000000000000000000000000000000000000000000000008152508585806135a290614922565b9650815181106135b5576135b461482e565b5b6020026020010181905250818585806135cd90614922565b9650815181106135e0576135df61482e565b5b60200260200101819052506040518060400160405280600681526020017f2c227270223a000000000000000000000000000000000000000000000000000081525085858061362d90614922565b9650815181106136405761363f61482e565b5b60200260200101819052508085858061365890614922565b96508151811061366b5761366a61482e565b5b60200260200101819052506040518060400160405280600681526020017f2c226f223a2200000000000000000000000000000000000000000000000000008152508585806136b890614922565b9650815181106136cb576136ca61482e565b5b60200260200101819052506136ec6136e789610100015161121a565b610463565b8585806136f890614922565b96508151811061370b5761370a61482e565b5b60200260200101819052508615613781576040518060400160405280600281526020017f227d00000000000000000000000000000000000000000000000000000000000081525085858061375e90614922565b9650815181106137715761377061482e565b5b60200260200101819052506137e2565b6040518060400160405280600381526020017f227d2c00000000000000000000000000000000000000000000000000000000008152508585806137c390614922565b9650815181106137d6576137d561482e565b5b60200260200101819052505b6137ec8585611a5a565b9550505050505092915050565b60606000600260078054905061380f91906148ee565b67ffffffffffffffff81111561382857613827613e68565b5b60405190808252806020026020018201604052801561385b57816020015b60608152602001906001900390816138465790505b5090506040518060400160405280600181526020017f5b00000000000000000000000000000000000000000000000000000000000000815250816000815181106138a8576138a761482e565b5b60200260200101819052506000600780549050905060006001905060005b82811015613b9357613b4a60056000600784815481106138e9576138e861482e565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206040518061018001604052908160008201805461396a90614999565b80601f016020809104026020016040519081016040528092919081815260200182805461399690614999565b80156139e35780601f106139b8576101008083540402835291602001916139e3565b820191906000526020600020905b8154815290600101906020018083116139c657829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152602001600682015481526020016007820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001600882015481525050600185613b43919061500b565b8314612eb3565b84600183613b5891906148ee565b81518110613b6957613b6861482e565b5b602002602001018190525081613b7e90614922565b91508080613b8b90614922565b9150506138c6565b506040518060400160405280600181526020017f5d00000000000000000000000000000000000000000000000000000000000000815250838281518110613bdd57613bdc61482e565b5b6020026020010181905250613bfe83600183613bf991906148ee565b611a5a565b935050505090565b7f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e356001604051613c369190614c83565b60405180910390a1806004819055507f3dd308df0922d63f3d70e40190514365cc399dfb3e3d7933543c8af6aa735e356002604051613c759190614cd9565b60405180910390a150565b6005602052806000526040600020600091509050806000018054613ca390614999565b80601f0160208091040260200160405190810160405280929190818152602001828054613ccf90614999565b8015613d1c5780601f10613cf157610100808354040283529160200191613d1c565b820191906000526020600020905b815481529060010190602001808311613cff57829003601f168201915b5050505050908060010160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16908060020154908060030154908060040160009054906101000a900460ff16908060040160019054906101000a900460ff16908060040160029054906101000a900460ff16908060040160039054906101000a900460ff16908060050154908060060154908060070160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1690806008015490508c565b508054613de890614999565b6000825580601f10613dfa5750613e19565b601f016020900490600052602060002090810190613e189190613e1c565b5b50565b5b80821115613e35576000816000905550600101613e1d565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b613ea082613e57565b810181811067ffffffffffffffff82111715613ebf57613ebe613e68565b5b80604052505050565b6000613ed2613e39565b9050613ede8282613e97565b919050565b600067ffffffffffffffff821115613efe57613efd613e68565b5b613f0782613e57565b9050602081019050919050565b82818337600083830152505050565b6000613f36613f3184613ee3565b613ec8565b905082815260208101848484011115613f5257613f51613e52565b5b613f5d848285613f14565b509392505050565b600082601f830112613f7a57613f79613e4d565b5b8135613f8a848260208601613f23565b91505092915050565b600060208284031215613fa957613fa8613e43565b5b600082013567ffffffffffffffff811115613fc757613fc6613e48565b5b613fd384828501613f65565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b83811015614016578082015181840152602081019050613ffb565b60008484015250505050565b600061402d82613fdc565b6140378185613fe7565b9350614047818560208601613ff8565b61405081613e57565b840191505092915050565b600060208201905081810360008301526140758184614022565b905092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b60006140a88261407d565b9050919050565b6140b88161409d565b81146140c357600080fd5b50565b6000813590506140d5816140af565b92915050565b6000602082840312156140f1576140f0613e43565b5b60006140ff848285016140c6565b91505092915050565b6000819050919050565b61411b81614108565b811461412657600080fd5b50565b60008135905061413881614112565b92915050565b60006020828403121561415457614153613e43565b5b600061416284828501614129565b91505092915050565b61417481614108565b82525050565b600060208201905061418f600083018461416b565b92915050565b600067ffffffffffffffff8211156141b0576141af613e68565b5b602082029050602081019050919050565b600080fd5b60006141d96141d484614195565b613ec8565b905080838252602082019050602084028301858111156141fc576141fb6141c1565b5b835b8181101561424357803567ffffffffffffffff81111561422157614220613e4d565b5b80860161422e8982613f65565b855260208501945050506020810190506141fe565b5050509392505050565b600082601f83011261426257614261613e4d565b5b81356142728482602086016141c6565b91505092915050565b6000806040838503121561429257614291613e43565b5b600083013567ffffffffffffffff8111156142b0576142af613e48565b5b6142bc8582860161424d565b92505060206142cd85828601614129565b9150509250929050565b60008115159050919050565b6142ec816142d7565b82525050565b600060208201905061430760008301846142e3565b92915050565b6143168161409d565b82525050565b6000602082019050614331600083018461430d565b92915050565b6000806040838503121561434e5761434d613e43565b5b600083013567ffffffffffffffff81111561436c5761436b613e48565b5b61437885828601613f65565b925050602061438985828601614129565b9150509250929050565b600061439e8261407d565b9050919050565b6143ae81614393565b81146143b957600080fd5b50565b6000813590506143cb816143a5565b92915050565b600080604083850312156143e8576143e7613e43565b5b60006143f6858286016143bc565b925050602061440785828601614129565b9150509250929050565b600067ffffffffffffffff82111561442c5761442b613e68565b5b602082029050602081019050919050565b600061445061444b84614411565b613ec8565b90508083825260208201905060208402830185811115614473576144726141c1565b5b835b8181101561449c578061448888826140c6565b845260208401935050602081019050614475565b5050509392505050565b600082601f8301126144bb576144ba613e4d565b5b81356144cb84826020860161443d565b91505092915050565b6000602082840312156144ea576144e9613e43565b5b600082013567ffffffffffffffff81111561450857614507613e48565b5b614514848285016144a6565b91505092915050565b600080fd5b600080fd5b614530816142d7565b811461453b57600080fd5b50565b60008135905061454d81614527565b92915050565b6000610180828403121561456a5761456961451d565b5b614575610180613ec8565b9050600082013567ffffffffffffffff81111561459557614594614522565b5b6145a184828501613f65565b60008301525060206145b5848285016143bc565b60208301525060406145c984828501614129565b60408301525060606145dd84828501614129565b60608301525060806145f18482850161453e565b60808301525060a06146058482850161453e565b60a08301525060c06146198482850161453e565b60c08301525060e061462d8482850161453e565b60e08301525061010061464284828501614129565b6101008301525061012061465884828501614129565b6101208301525061014061466e848285016140c6565b6101408301525061016061468484828501614129565b6101608301525092915050565b600080604083850312156146a8576146a7613e43565b5b600083013567ffffffffffffffff8111156146c6576146c5613e48565b5b6146d285828601614553565b92505060206146e38582860161453e565b9150509250929050565b6146f681614393565b82525050565b6000610180820190508181036000830152614717818f614022565b9050614726602083018e6146ed565b614733604083018d61416b565b614740606083018c61416b565b61474d608083018b6142e3565b61475a60a083018a6142e3565b61476760c08301896142e3565b61477460e08301886142e3565b61478261010083018761416b565b61479061012083018661416b565b61479e61014083018561430d565b6147ac61016083018461416b565b9d9c50505050505050505050505050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006147f782614108565b915061480283614108565b925082820261481081614108565b91508282048414831517614827576148266147bd565b5b5092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b600061489782614108565b91506148a283614108565b9250826148b2576148b161485d565b5b828204905092915050565b60006148c882614108565b91506148d383614108565b9250826148e3576148e261485d565b5b828206905092915050565b60006148f982614108565b915061490483614108565b925082820190508082111561491c5761491b6147bd565b5b92915050565b600061492d82614108565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff820361495f5761495e6147bd565b5b600182019050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b600060028204905060018216806149b157607f821691505b6020821081036149c4576149c361496a565b5b50919050565b60008190508160005260206000209050919050565b60006020601f8301049050919050565b600082821b905092915050565b600060088302614a2c7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff826149ef565b614a3686836149ef565b95508019841693508086168417925050509392505050565b6000819050919050565b6000614a73614a6e614a6984614108565b614a4e565b614108565b9050919050565b6000819050919050565b614a8d83614a58565b614aa1614a9982614a7a565b8484546149fc565b825550505050565b600090565b614ab6614aa9565b614ac1818484614a84565b505050565b5b81811015614ae557614ada600082614aae565b600181019050614ac7565b5050565b601f821115614b2a57614afb816149ca565b614b04846149df565b81016020851015614b13578190505b614b27614b1f856149df565b830182614ac6565b50505b505050565b600082821c905092915050565b6000614b4d60001984600802614b2f565b1980831691505092915050565b6000614b668383614b3c565b9150826002028217905092915050565b614b7f82613fdc565b67ffffffffffffffff811115614b9857614b97613e68565b5b614ba28254614999565b614bad828285614ae9565b600060209050601f831160018114614be05760008415614bce578287015190505b614bd88582614b5a565b865550614c40565b601f198416614bee866149ca565b60005b82811015614c1657848901518255600182019150602085019450602081019050614bf1565b86831015614c335784890151614c2f601f891682614b3c565b8355505b6001600288020188555050505b505050505050565b6000819050919050565b6000614c6d614c68614c6384614c48565b614a4e565b614108565b9050919050565b614c7d81614c52565b82525050565b6000602082019050614c986000830184614c74565b92915050565b6000819050919050565b6000614cc3614cbe614cb984614c9e565b614a4e565b614108565b9050919050565b614cd381614ca8565b82525050565b6000602082019050614cee6000830184614cca565b92915050565b6000819050919050565b6000614d19614d14614d0f84614cf4565b614a4e565b614108565b9050919050565b614d2981614cfe565b82525050565b6000602082019050614d446000830184614d20565b92915050565b6000819050919050565b6000614d6f614d6a614d6584614d4a565b614a4e565b614108565b9050919050565b614d7f81614d54565b82525050565b6000602082019050614d9a6000830184614d76565b92915050565b6000819050919050565b6000614dc5614dc0614dbb84614da0565b614a4e565b614108565b9050919050565b614dd581614daa565b82525050565b6000602082019050614df06000830184614dcc565b92915050565b6000819050919050565b6000614e1b614e16614e1184614df6565b614a4e565b614108565b9050919050565b614e2b81614e00565b82525050565b6000602082019050614e466000830184614e22565b92915050565b6000819050919050565b6000614e71614e6c614e6784614e4c565b614a4e565b614108565b9050919050565b614e8181614e56565b82525050565b6000602082019050614e9c6000830184614e78565b92915050565b6000819050919050565b6000614ec7614ec2614ebd84614ea2565b614a4e565b614108565b9050919050565b614ed781614eac565b82525050565b6000602082019050614ef26000830184614ece565b92915050565b6000819050919050565b6000614f1d614f18614f1384614ef8565b614a4e565b614108565b9050919050565b614f2d81614f02565b82525050565b6000602082019050614f486000830184614f24565b92915050565b60008160601b9050919050565b6000614f6682614f4e565b9050919050565b6000614f7882614f5b565b9050919050565b614f90614f8b8261409d565b614f6d565b82525050565b6000614fa28284614f7f565b60148201915081905092915050565b600060a082019050614fc6600083018861430d565b8181036020830152614fd88187614022565b9050614fe7604083018661416b565b614ff4606083018561416b565b615001608083018461416b565b9695505050505050565b600061501682614108565b915061502183614108565b9250828203905081811115615039576150386147bd565b5b9291505056fea264697066735822122057e268e627f3197cfa0b1bb12935458283c412a7183895284043621f512ccd4e64736f6c63430008110033000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000002540be4000000000000000000000000000000000000000000000000000000000005f5e1000000000000000000000000000000000000000000000000000000000000000003000000000000000000000000e252d01a37b85e2007ed3cc13797aa92496204a40000000000000000000000005f15294a1918633d4dd4ec47098a14d01c58e957000000000000000000000000d45cfd6855c6ec8f635a6f2b46c647e99c59c79d");
}

if (args[0] == 2) {
    call_contract(args[1], args[2]);
}

if (args[0] == 3) {
    CreatePhr();
}

if (args[0] == 4) {
    Prepayment(args[1]);
}

// 测试event
if (args[0] == 7) {
    call_contract("608060405260405162004115380380620041158339818101604052810190620000299190620003a0565b60008251905060005b81811015620000c65760016004600086848151811062000057576200005662000406565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff0219169083151502179055508080620000bd9062000464565b91505062000032565b50600080819055506001600460003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff0219169083151502179055508160028190555033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550505050620004b1565b6000604051905090565b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b620001db8262000190565b810181811067ffffffffffffffff82111715620001fd57620001fc620001a1565b5b80604052505050565b60006200021262000177565b9050620002208282620001d0565b919050565b600067ffffffffffffffff821115620002435762000242620001a1565b5b602082029050602081019050919050565b600080fd5b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620002868262000259565b9050919050565b620002988162000279565b8114620002a457600080fd5b50565b600081519050620002b8816200028d565b92915050565b6000620002d5620002cf8462000225565b62000206565b90508083825260208201905060208402830185811115620002fb57620002fa62000254565b5b835b81811015620003285780620003138882620002a7565b845260208401935050602081019050620002fd565b5050509392505050565b600082601f8301126200034a57620003496200018b565b5b81516200035c848260208601620002be565b91505092915050565b6000819050919050565b6200037a8162000365565b81146200038657600080fd5b50565b6000815190506200039a816200036f565b92915050565b60008060408385031215620003ba57620003b962000181565b5b600083015167ffffffffffffffff811115620003db57620003da62000186565b5b620003e98582860162000332565b9250506020620003fc8582860162000389565b9150509250929050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b6000620004718262000365565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203620004a657620004a562000435565b5b600182019050919050565b613c5480620004c16000396000f3fe6080604052600436106100fe5760003560e01c8063856ed09411610095578063b3c7fdee11610064578063b3c7fdee14610311578063bde73c391461034e578063c77927c91461036a578063cdfd45bb14610393578063f40e8471146103be576100fe565b8063856ed094146102645780638da5cb5b146102a1578063ae7abc80146102cc578063b1276e6d146102e8576100fe565b80634e64f75b116100d15780634e64f75b146101a357806351fdd191146101bf578063593b79fe146101ea57806370587c1014610227576100fe565b80630cdb0d53146101035780630f85011c146101405780632e1e5dbe1461014a5780632e559c0d14610166575b600080fd5b34801561010f57600080fd5b5061012a60048036038101906101259190612f41565b610403565b6040516101379190613009565b60405180910390f35b610148610609565b005b610164600480360381019061015f9190613089565b610a4d565b005b34801561017257600080fd5b5061018d600480360381019061018891906130ec565b610f0c565b60405161019a9190613009565b60405180910390f35b6101bd60048036038101906101b89190613089565b610f69565b005b3480156101cb57600080fd5b506101d4611417565b6040516101e19190613128565b60405180910390f35b3480156101f657600080fd5b50610211600480360381019061020c9190613089565b61141d565b60405161021e9190613009565b60405180910390f35b34801561023357600080fd5b5061024e60048036038101906102499190613229565b611446565b60405161025b9190613009565b60405180910390f35b34801561027057600080fd5b5061028b60048036038101906102869190613089565b6115ce565b60405161029891906132a0565b60405180910390f35b3480156102ad57600080fd5b506102b66115ee565b6040516102c391906132ca565b60405180910390f35b6102e660048036038101906102e191906132e5565b611614565b005b3480156102f457600080fd5b5061030f600480360381019061030a9190613089565b6118f4565b005b34801561031d57600080fd5b50610338600480360381019061033391906134b1565b611a05565b6040516103459190613009565b60405180910390f35b6103686004803603810190610363919061350d565b61216b565b005b34801561037657600080fd5b50610391600480360381019061038c9190613610565b61277a565b005b34801561039f57600080fd5b506103a86128c9565b6040516103b59190613009565b60405180910390f35b3480156103ca57600080fd5b506103e560048036038101906103e09190613089565b612c60565b6040516103fa99989796959493929190613668565b60405180910390f35b6060600060028351610415919061372b565b67ffffffffffffffff81111561042e5761042d612e16565b5b6040519080825280601f01601f1916602001820160405280156104605781602001600182028036833780820191505090505b50905060006040518060400160405280601081526020017f3031323334353637383961626364656600000000000000000000000000000000815250905060005b84518110156105fe578182518683815181106104bf576104be61376d565b5b602001015160f81c60f81b60f81c60ff166104da91906137cb565b815181106104eb576104ea61376d565b5b602001015160f81c60f81b83600283610504919061372b565b815181106105155761051461376d565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a90535081825186838151811061055a5761055961376d565b5b602001015160f81c60f81b60f81c60ff1661057591906137fc565b815181106105865761058561376d565b5b602001015160f81c60f81b8360016002846105a1919061372b565b6105ab919061382d565b815181106105bc576105bb61376d565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a90535080806105f690613861565b9150506104a0565b508192505050919050565b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1661066257600080fd5b6000600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020604051806101200160405290816000820180546106bf906138d8565b80601f01602080910402602001604051908101604052809291908181526020018280546106eb906138d8565b80156107385780601f1061070d57610100808354040283529160200191610738565b820191906000526020600020905b81548152906001019060200180831161071b57829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff16151515158152602001600582015481525050905060018160a0019015159081151581525050806080015115610a4a573373ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f19350505050158015610888573d6000803e3d6000fd5b506000600580549050905060005b81811015610966573373ffffffffffffffffffffffffffffffffffffffff16600582815481106108c9576108c861376d565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff160361095557600581815481106109235761092261376d565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055610966565b8061095f90613861565b9050610896565b50600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600080820160006109b79190612d8a565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff021916905560058201600090555050505b50565b600360008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff16610aa657600080fd5b600460003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff16610afc57600080fd5b6000600360008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060405180610120016040529081600082018054610b59906138d8565b80601f0160208091040260200160405190810160405280929190818152602001828054610b85906138d8565b8015610bd25780601f10610ba757610100808354040283529160200191610bd2565b820191906000526020600020905b815481529060010190602001808311610bb557829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152505090508173ffffffffffffffffffffffffffffffffffffffff16816020015173ffffffffffffffffffffffffffffffffffffffff1614610cf957600080fd5b806020015173ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f19350505050158015610d47573d6000803e3d6000fd5b506000600580549050905060005b81811015610e25578373ffffffffffffffffffffffffffffffffffffffff1660058281548110610d8857610d8761376d565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603610e145760058181548110610de257610de161376d565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055610e25565b80610e1e90613861565b9050610d55565b50600360008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008082016000610e769190612d8a565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff021916905560058201600090555050505050565b6060602067ffffffffffffffff811115610f2957610f28612e16565b5b6040519080825280601f01601f191660200182016040528015610f5b5781602001600182028036833780820191505090505b509050816020820152919050565b600360008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff16610fc257600080fd5b600460003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff1661101857600080fd5b6000600360008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060405180610120016040529081600082018054611075906138d8565b80601f01602080910402602001604051908101604052809291908181526020018280546110a1906138d8565b80156110ee5780601f106110c3576101008083540402835291602001916110ee565b820191906000526020600020905b8154815290600101906020018083116110d157829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff1615151515815260200160058201548152505090508060e00151156111e857600080fd5b60018160800190151590811515815250508060a001511561141357806020015173ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f19350505050158015611251573d6000803e3d6000fd5b506000600580549050905060005b8181101561132f578373ffffffffffffffffffffffffffffffffffffffff16600582815481106112925761129161376d565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff160361131e57600581815481106112ec576112eb61376d565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff021916905561132f565b8061132890613861565b905061125f565b50600360008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600080820160006113809190612d8a565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff021916905560058201600090555050505b5050565b60025481565b6060816040516020016114309190613951565b6040516020818303038152906040529050919050565b60606000805b83811015611490578481815181106114675761146661376d565b5b6020026020010151518261147b919061382d565b9150808061148890613861565b91505061144c565b5060008167ffffffffffffffff8111156114ad576114ac612e16565b5b6040519080825280601f01601f1916602001820160405280156114df5781602001600182028036833780820191505090505b5090506000805b858110156115c15760005b8782815181106115045761150361376d565b5b6020026020010151518110156115ad578782815181106115275761152661376d565b5b602002602001015181815181106115415761154061376d565b5b602001015160f81c60f81b84848061155890613861565b95508151811061156b5761156a61376d565b5b60200101907effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1916908160001a90535080806115a590613861565b9150506114f1565b5080806115b990613861565b9150506114e6565b5081935050505092915050565b60046020528060005260406000206000915054906101000a900460ff1681565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60025434101561162357600080fd5b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff161561167d57600080fd5b600460003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900460ff16156116d457600080fd5b6040518061012001604052808381526020013373ffffffffffffffffffffffffffffffffffffffff168152602001348152602001828152602001600015158152602001600015158152602001600115158152602001600015158152602001600054815250600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600082015181600001908161178c9190613b18565b5060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550604082015181600201556060820151816003015560808201518160040160006101000a81548160ff02191690831515021790555060a08201518160040160016101000a81548160ff02191690831515021790555060c08201518160040160026101000a81548160ff02191690831515021790555060e08201518160040160036101000a81548160ff02191690831515021790555061010082015181600501559050506005339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506000808154809291906118eb90613861565b91905055505050565b600360008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff1661194d57600080fd5b600360008273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160039054906101000a900460ff16156119a757600080fd5b6001600360008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160036101000a81548160ff02191690831515021790555050565b60606000606467ffffffffffffffff811115611a2457611a23612e16565b5b604051908082528060200260200182016040528015611a5757816020015b6060815260200190600190039081611a425790505b50905060006040518060400160405280600681526020017f7b2272223a220000000000000000000000000000000000000000000000000000815250828280611a9e90613861565b935081518110611ab157611ab061376d565b5b6020026020010181905250611ac98560000151610403565b828280611ad590613861565b935081518110611ae857611ae761376d565b5b60200260200101819052506040518060400160405280600781526020017f222c2261223a2200000000000000000000000000000000000000000000000000815250828280611b3590613861565b935081518110611b4857611b4761376d565b5b6020026020010181905250611b68611b63866020015161141d565b610403565b828280611b7490613861565b935081518110611b8757611b8661376d565b5b60200260200101819052506040518060400160405280600681526020017f222c226d223a0000000000000000000000000000000000000000000000000000815250828280611bd490613861565b935081518110611be757611be661376d565b5b6020026020010181905250611c07611c028660400151610f0c565b610403565b828280611c1390613861565b935081518110611c2657611c2561376d565b5b60200260200101819052506040518060400160405280600681526020017f222c2270223a0000000000000000000000000000000000000000000000000000815250828280611c7390613861565b935081518110611c8657611c8561376d565b5b6020026020010181905250611ca6611ca18660600151610f0c565b610403565b828280611cb290613861565b935081518110611cc557611cc461376d565b5b602002602001018190525060006040518060400160405280600581526020017f66616c73650000000000000000000000000000000000000000000000000000008152509050856080015115611d4d576040518060400160405280600481526020017f747275650000000000000000000000000000000000000000000000000000000081525090505b60006040518060400160405280600581526020017f66616c736500000000000000000000000000000000000000000000000000000081525090508660a0015115611dca576040518060400160405280600481526020017f747275650000000000000000000000000000000000000000000000000000000081525090505b60006040518060400160405280600581526020017f66616c736500000000000000000000000000000000000000000000000000000081525090508760e0015115611e47576040518060400160405280600481526020017f747275650000000000000000000000000000000000000000000000000000000081525090505b6040518060400160405280600781526020017f222c226d72223a00000000000000000000000000000000000000000000000000815250858580611e8990613861565b965081518110611e9c57611e9b61376d565b5b602002602001018190525082858580611eb490613861565b965081518110611ec757611ec661376d565b5b60200260200101819052506040518060400160405280600681526020017f2c227372223a0000000000000000000000000000000000000000000000000000815250858580611f1490613861565b965081518110611f2757611f2661376d565b5b602002602001018190525081858580611f3f90613861565b965081518110611f5257611f5161376d565b5b60200260200101819052506040518060400160405280600681526020017f2c227270223a0000000000000000000000000000000000000000000000000000815250858580611f9f90613861565b965081518110611fb257611fb161376d565b5b602002602001018190525080858580611fca90613861565b965081518110611fdd57611fdc61376d565b5b60200260200101819052506040518060400160405280600681526020017f2c226f223a22000000000000000000000000000000000000000000000000000081525085858061202a90613861565b96508151811061203d5761203c61376d565b5b602002602001018190525061205e612059896101000151610f0c565b610403565b85858061206a90613861565b96508151811061207d5761207c61376d565b5b602002602001018190525086156120f3576040518060400160405280600281526020017f227d0000000000000000000000000000000000000000000000000000000000008152508585806120d090613861565b9650815181106120e3576120e261376d565b5b6020026020010181905250612154565b6040518060400160405280600381526020017f227d2c000000000000000000000000000000000000000000000000000000000081525085858061213590613861565b9650815181106121485761214761376d565b5b60200260200101819052505b61215e8585611446565b9550505050505092915050565b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff166121c457600080fd5b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160009054906101000a900460ff161561221e57600080fd5b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160019054906101000a900460ff161561227857600080fd5b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160039054906101000a900460ff16156122d257600080fd5b80600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060020154101561232157600080fd5b6000600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206040518061012001604052908160008201805461237e906138d8565b80601f01602080910402602001604051908101604052809291908181526020018280546123aa906138d8565b80156123f75780601f106123cc576101008083540402835291602001916123f7565b820191906000526020600020905b8154815290600101906020018083116123da57829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff16151515158152602001600582015481525050905081816040018181516124f49190613bea565b915081815250508273ffffffffffffffffffffffffffffffffffffffff166108fc839081150290604051600060405180830381858888f19350505050158015612541573d6000803e3d6000fd5b5060025481604001511015612775576000816040015111156125a9573373ffffffffffffffffffffffffffffffffffffffff166108fc82604001519081150290604051600060405180830381858888f193505050501580156125a7573d6000803e3d6000fd5b505b60008160400181815250506000600580549050905060005b81811015612691573373ffffffffffffffffffffffffffffffffffffffff16600582815481106125f4576125f361376d565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1603612680576005818154811061264e5761264d61376d565b5b9060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055612691565b8061268a90613861565b90506125c1565b50600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020600080820160006126e29190612d8a565b6001820160006101000a81549073ffffffffffffffffffffffffffffffffffffffff0219169055600282016000905560038201600090556004820160006101000a81549060ff02191690556004820160016101000a81549060ff02191690556004820160026101000a81549060ff02191690556004820160036101000a81549060ff021916905560058201600090555050505b505050565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16146127d457600080fd5b600360003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060040160029054906101000a900460ff161561282e57600080fd5b60008151905060005b818110156128c4576001600460008584815181106128585761285761376d565b5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548160ff02191690831515021790555080806128bc90613861565b915050612837565b505050565b6060600060026005805490506128df919061382d565b67ffffffffffffffff8111156128f8576128f7612e16565b5b60405190808252806020026020018201604052801561292b57816020015b60608152602001906001900390816129165790505b5090506040518060400160405280600181526020017f5b00000000000000000000000000000000000000000000000000000000000000815250816000815181106129785761297761376d565b5b60200260200101819052506000600580549050905060006001905060005b82811015612bed57612ba460036000600584815481106129b9576129b861376d565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060405180610120016040529081600082018054612a3a906138d8565b80601f0160208091040260200160405190810160405280929190818152602001828054612a66906138d8565b8015612ab35780601f10612a8857610100808354040283529160200191612ab3565b820191906000526020600020905b815481529060010190602001808311612a9657829003601f168201915b505050505081526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200160028201548152602001600382015481526020016004820160009054906101000a900460ff161515151581526020016004820160019054906101000a900460ff161515151581526020016004820160029054906101000a900460ff161515151581526020016004820160039054906101000a900460ff16151515158152602001600582015481525050848314611a05565b84600183612bb2919061382d565b81518110612bc357612bc261376d565b5b602002602001018190525081612bd890613861565b91508080612be590613861565b915050612996565b506040518060400160405280600181526020017f5d00000000000000000000000000000000000000000000000000000000000000815250838281518110612c3757612c3661376d565b5b6020026020010181905250612c5883600183612c53919061382d565b611446565b935050505090565b6003602052806000526040600020600091509050806000018054612c83906138d8565b80601f0160208091040260200160405190810160405280929190818152602001828054612caf906138d8565b8015612cfc5780601f10612cd157610100808354040283529160200191612cfc565b820191906000526020600020905b815481529060010190602001808311612cdf57829003601f168201915b5050505050908060010160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16908060020154908060030154908060040160009054906101000a900460ff16908060040160019054906101000a900460ff16908060040160029054906101000a900460ff16908060040160039054906101000a900460ff16908060050154905089565b508054612d96906138d8565b6000825580601f10612da85750612dc7565b601f016020900490600052602060002090810190612dc69190612dca565b5b50565b5b80821115612de3576000816000905550600101612dcb565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b612e4e82612e05565b810181811067ffffffffffffffff82111715612e6d57612e6c612e16565b5b80604052505050565b6000612e80612de7565b9050612e8c8282612e45565b919050565b600067ffffffffffffffff821115612eac57612eab612e16565b5b612eb582612e05565b9050602081019050919050565b82818337600083830152505050565b6000612ee4612edf84612e91565b612e76565b905082815260208101848484011115612f0057612eff612e00565b5b612f0b848285612ec2565b509392505050565b600082601f830112612f2857612f27612dfb565b5b8135612f38848260208601612ed1565b91505092915050565b600060208284031215612f5757612f56612df1565b5b600082013567ffffffffffffffff811115612f7557612f74612df6565b5b612f8184828501612f13565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b83811015612fc4578082015181840152602081019050612fa9565b60008484015250505050565b6000612fdb82612f8a565b612fe58185612f95565b9350612ff5818560208601612fa6565b612ffe81612e05565b840191505092915050565b600060208201905081810360008301526130238184612fd0565b905092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b60006130568261302b565b9050919050565b6130668161304b565b811461307157600080fd5b50565b6000813590506130838161305d565b92915050565b60006020828403121561309f5761309e612df1565b5b60006130ad84828501613074565b91505092915050565b6000819050919050565b6130c9816130b6565b81146130d457600080fd5b50565b6000813590506130e6816130c0565b92915050565b60006020828403121561310257613101612df1565b5b6000613110848285016130d7565b91505092915050565b613122816130b6565b82525050565b600060208201905061313d6000830184613119565b92915050565b600067ffffffffffffffff82111561315e5761315d612e16565b5b602082029050602081019050919050565b600080fd5b600061318761318284613143565b612e76565b905080838252602082019050602084028301858111156131aa576131a961316f565b5b835b818110156131f157803567ffffffffffffffff8111156131cf576131ce612dfb565b5b8086016131dc8982612f13565b855260208501945050506020810190506131ac565b5050509392505050565b600082601f8301126132105761320f612dfb565b5b8135613220848260208601613174565b91505092915050565b600080604083850312156132405761323f612df1565b5b600083013567ffffffffffffffff81111561325e5761325d612df6565b5b61326a858286016131fb565b925050602061327b858286016130d7565b9150509250929050565b60008115159050919050565b61329a81613285565b82525050565b60006020820190506132b56000830184613291565b92915050565b6132c48161304b565b82525050565b60006020820190506132df60008301846132bb565b92915050565b600080604083850312156132fc576132fb612df1565b5b600083013567ffffffffffffffff81111561331a57613319612df6565b5b61332685828601612f13565b9250506020613337858286016130d7565b9150509250929050565b600080fd5b600080fd5b60006133568261302b565b9050919050565b6133668161334b565b811461337157600080fd5b50565b6000813590506133838161335d565b92915050565b61339281613285565b811461339d57600080fd5b50565b6000813590506133af81613389565b92915050565b600061012082840312156133cc576133cb613341565b5b6133d7610120612e76565b9050600082013567ffffffffffffffff8111156133f7576133f6613346565b5b61340384828501612f13565b600083015250602061341784828501613374565b602083015250604061342b848285016130d7565b604083015250606061343f848285016130d7565b6060830152506080613453848285016133a0565b60808301525060a0613467848285016133a0565b60a08301525060c061347b848285016133a0565b60c08301525060e061348f848285016133a0565b60e0830152506101006134a4848285016130d7565b6101008301525092915050565b600080604083850312156134c8576134c7612df1565b5b600083013567ffffffffffffffff8111156134e6576134e5612df6565b5b6134f2858286016133b5565b9250506020613503858286016133a0565b9150509250929050565b6000806040838503121561352457613523612df1565b5b600061353285828601613374565b9250506020613543858286016130d7565b9150509250929050565b600067ffffffffffffffff82111561356857613567612e16565b5b602082029050602081019050919050565b600061358c6135878461354d565b612e76565b905080838252602082019050602084028301858111156135af576135ae61316f565b5b835b818110156135d857806135c48882613074565b8452602084019350506020810190506135b1565b5050509392505050565b600082601f8301126135f7576135f6612dfb565b5b8135613607848260208601613579565b91505092915050565b60006020828403121561362657613625612df1565b5b600082013567ffffffffffffffff81111561364457613643612df6565b5b613650848285016135e2565b91505092915050565b6136628161334b565b82525050565b6000610120820190508181036000830152613683818c612fd0565b9050613692602083018b613659565b61369f604083018a613119565b6136ac6060830189613119565b6136b96080830188613291565b6136c660a0830187613291565b6136d360c0830186613291565b6136e060e0830185613291565b6136ee610100830184613119565b9a9950505050505050505050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b6000613736826130b6565b9150613741836130b6565b925082820261374f816130b6565b91508282048414831517613766576137656136fc565b5b5092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b60006137d6826130b6565b91506137e1836130b6565b9250826137f1576137f061379c565b5b828204905092915050565b6000613807826130b6565b9150613812836130b6565b9250826138225761382161379c565b5b828206905092915050565b6000613838826130b6565b9150613843836130b6565b925082820190508082111561385b5761385a6136fc565b5b92915050565b600061386c826130b6565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff820361389e5761389d6136fc565b5b600182019050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b600060028204905060018216806138f057607f821691505b602082108103613903576139026138a9565b5b50919050565b60008160601b9050919050565b600061392182613909565b9050919050565b600061393382613916565b9050919050565b61394b6139468261304b565b613928565b82525050565b600061395d828461393a565b60148201915081905092915050565b60008190508160005260206000209050919050565b60006020601f8301049050919050565b600082821b905092915050565b6000600883026139ce7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82613991565b6139d88683613991565b95508019841693508086168417925050509392505050565b6000819050919050565b6000613a15613a10613a0b846130b6565b6139f0565b6130b6565b9050919050565b6000819050919050565b613a2f836139fa565b613a43613a3b82613a1c565b84845461399e565b825550505050565b600090565b613a58613a4b565b613a63818484613a26565b505050565b5b81811015613a8757613a7c600082613a50565b600181019050613a69565b5050565b601f821115613acc57613a9d8161396c565b613aa684613981565b81016020851015613ab5578190505b613ac9613ac185613981565b830182613a68565b50505b505050565b600082821c905092915050565b6000613aef60001984600802613ad1565b1980831691505092915050565b6000613b088383613ade565b9150826002028217905092915050565b613b2182612f8a565b67ffffffffffffffff811115613b3a57613b39612e16565b5b613b4482546138d8565b613b4f828285613a8b565b600060209050601f831160018114613b825760008415613b70578287015190505b613b7a8582613afc565b865550613be2565b601f198416613b908661396c565b60005b82811015613bb857848901518255600182019150602085019450602081019050613b93565b86831015613bd55784890151613bd1601f891682613ade565b8355505b6001600288020188555050505b505050505050565b6000613bf5826130b6565b9150613c00836130b6565b9250828203905081811115613c1857613c176136fc565b5b9291505056fea2646970667358221220ebc96bc949901bb155ba34acf39abf73c68041f696eb205be5c64c07fa31774a64736f6c634300081100330000000000000000000000000000000000000000000000000000000000000040000000000000000000000000000000000000000000000000000000746a5288000000000000000000000000000000000000000000000000000000000000000003000000000000000000000000e252d01a37b85e2007ed3cc13797aa92496204a40000000000000000000000005f15294a1918633d4dd4ec47098a14d01c58e957000000000000000000000000d45cfd6855c6ec8f635a6f2b46c647e99c59c79d", prikey, 0);
}


if (args[0] == 9) {

}
