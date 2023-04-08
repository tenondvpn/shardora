const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path
const { randomBytes } = require('crypto')
const Secp256k1 = require('./secp256k1_1')
const keccak256 = require('keccak256')
var querystring = require('querystring');
var http = require('http');
var fs = require('fs');

var addParamCode = web3.eth.abi.encodeFunctionSignature('callAbe(bytes)');


const isHexadecimal = str => /^[A-F0-9]+$/i.test(str);
const isNumber = str => /^[0-9]+$/i.test(str);
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
    const privateKeyBuf = Secp256k1.uint256("fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971", 16)
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

function create_call_function(gid, to, gas_limit, gas_price, call_param) {
    var tx_type = 6;
    var msg = gid + "-" +
        self_account_id + "-" +
        to + "-" +
        '0' + "-" +
        gas_limit + "-" +
        gas_price + "-" +
        tx_type.toString() + "-";
    msg += str_to_hex("__cinput") + call_param;
    var kechash = keccak256(msg)
    var digest = Secp256k1.uint256(kechash, 16)
    const sig = Secp256k1.ecsign(self_private_key, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(self_public_key.x, 16)
    const pubY = Secp256k1.uint256(self_public_key.y, 16)
    const isValidSig = Secp256k1.ecverify(pubX, pubY, sigR, sigS, digest)
    if (!isValidSig) {
        console.log('signature transaction failed.');
        return;
    }

    console.log("get pk: " + '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16))
    return {
        'gid': gid,
        'frompk': '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16),
        'to': to,
        'amount': 0,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': local_count_shard_id,
        'hash': kechash,
        'attrs_size': 1,
        'key0': str_to_hex('__cinput'),
        "val0": call_param,
        'sigr': sigR.toString(16),
        'sigs': sigS.toString(16)
    }
}

function GetValidHexString(uint256_bytes) {
    var str_res = uint256_bytes.toString(16)
    while (str_res.length < 64) {
        str_res = "0" + str_res;
    }

    return str_res;
}

function PostCode(data) {
    var post_data = querystring.stringify(data);
    var post_options = {
        host: '82.156.224.174',
        port: '19098',
        path: '/do_transaction',
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
            } 
        })
    });

    console.log("req data: " + post_data);
    post_req.write(post_data);
    post_req.end();
}

function call_contract_function(call_param) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var data = create_call_function(
        gid,
        contract_address.toString(16),
        100000000,
        1,
        call_param);
    PostCode(data);
}

function create_contract(gid, to, amount, gas_limit, gas_price, contract_bytes, name, desc) {
    var contract_src = "";
    var tx_type = 4;
    var msg = gid + "-" +
        self_account_id + "-" +
        to + "-" +
        amount + "-" +
        gas_limit + "-" +
        gas_price + "-" +
        tx_type.toString() + "-";
    msg += str_to_hex("__cbytescode") + contract_bytes;
    msg += str_to_hex("__csourcecode");
    msg += str_to_hex("__ctname") + str_to_hex(name);
    msg += str_to_hex("__ctdesc") + str_to_hex(desc);
    var kechash = keccak256(msg)
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
        'frompk': '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': local_count_shard_id,
        'hash': kechash,
        'attrs_size': 4,
        'key0': str_to_hex('__cbytescode'),
        'key1': str_to_hex('__csourcecode'),
        'key2': str_to_hex('__ctname'),
        'key3': str_to_hex('__ctdesc'),
        "val0": contract_bytes,
        "val1": str_to_hex(""),
        "val2": str_to_hex(name),
        "val3": str_to_hex(desc),
        'sigr': sigR.toString(16),
        'sigs': sigS.toString(16)
    }
}

function call_create_contract(contract_bytes, name, desc) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var kechash = keccak256(self_account_id + gid + contract_bytes).toString('hex')
    var self_contract_address = kechash.slice(kechash.length - 40, kechash.length)
    var data = create_contract(
        gid,
        self_contract_address,
        10000000000,
        10000000,
        1,
        contract_bytes,
        name,
        desc);
    PostCode(data);

    const opt = { flag: 'w', }
    fs.writeFile('contract_address', self_contract_address, opt, (err) => {
        if (err) {
            console.error(err)
        }
    })
}

function create_tx(to, amount, gas_limit, gas_price) {
    var gid = GetValidHexString(Secp256k1.uint256(randomBytes(32)));
    var tx_type = 5;
    var msg = gid + "-" +
        self_account_id.toString(16) + "-" +
        to + "-" +
        amount + "-" +
        gas_limit + "-" +
        gas_price + "-" +
        tx_type.toString() + "-";
    var kechash = keccak256(msg)
    var digest = Secp256k1.uint256(kechash, 16)
    const sig = Secp256k1.ecsign(self_private_key, digest)
    const sigR = Secp256k1.uint256(sig.r, 16)
    const sigS = Secp256k1.uint256(sig.s, 16)
    const pubX = Secp256k1.uint256(self_public_key.x, 16)
    const pubY = Secp256k1.uint256(self_public_key.y, 16)
    const isValidSig = Secp256k1.ecverify(pubX, pubY, sigR, sigS, digest)
    if (!isValidSig) {
        Toast.fire({
            icon: 'error',
            title: 'signature transaction failed.'
        })

        return;
    }
    console.log("frompk: " + '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16) + ", msg: " + msg + ", kechash: " + kechash.toString('hex') + ", sigR:" + sigR.toString(16) + ", sigS:" + sigS.toString(16));
    return {
        'gid': gid,
        'frompk': '04' + self_public_key.x.toString(16) + self_public_key.y.toString(16),
        'to': to,
        'amount': amount,
        'gas_limit': gas_limit,
        'gas_price': gas_price,
        'type': tx_type,
        'shard_id': local_count_shard_id,
        'hash': kechash,
        'sigr': sigR.toString(16),
        'sigs': sigS.toString(16)
    }
}

function do_transaction(to_addr, amount, gas_limit, gas_price) {
    var data = create_tx(to_addr, amount, gas_limit, gas_price);
    PostCode(data);
}

init_private_key();

function call_verify(index) {
    try {
        const data = index
        var param1 = "pkeet";
        var hexparam1 = web3.utils.toHex(param1 + data);
        var addParam1 = web3.eth.abi.encodeParameter('bytes', hexparam1);
        console.log("call params: " + addParamCode.substring(2) + addParam1.substring(2);
        call_contract_function(addParamCode.substring(2) + addParam1.substring(2));
    } catch (err) {
        console.error(err);
    }
}

function add_pairing_param(key, value) {
    var key_len = key.length.toString();
    if (key.length <= 9) {
        key_len = "0" + key_len;
    }

    var param = "add" + key_len + key + value;
    var hexparam = web3.utils.toHex(param);
    var addParam = web3.eth.abi.encodeParameter('bytes', hexparam);
    //console.log("addParam 0: " + addParamCode.substring(2) + addParam.substring(2));
    call_contract_function(addParamCode.substring(2) + addParam.substring(2));

}

function set_all_params() {
    try {
        const data = fs.readFileSync('./gm', 'UTF-8');
        add_pairing_param('all', data);
    } catch (err) {
        console.error(err);
    }
}

const args = process.argv.slice(2)
console.log(args)

var pairing_param_value = "type a\nq 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\nh 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\nr 730750818665451621361119245571504901405976559617\nexp2 159\nexp1 107\nsign1 1\nsign0 1";
var contract_bytes = "6080604052604051610817380380610817833981810160405281019061002591906100b6565b816001819055508060028190555033600360006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050506100f6565b600080fd5b6000819050919050565b61009381610080565b811461009e57600080fd5b50565b6000815190506100b08161008a565b92915050565b600080604083850312156100cd576100cc61007b565b5b60006100db858286016100a1565b92505060206100ec858286016100a1565b9150509250929050565b610712806101056000396000f3fe6080604052600436106100345760003560e01c806302d05d3f146100395780634162d68f14610064578063d1e94e5714610080575b600080fd5b34801561004557600080fd5b5061004e6100bd565b60405161005b9190610344565b60405180910390f35b61007e600480360381019061007991906104b9565b6100e3565b005b34801561008c57600080fd5b506100a760048036038101906100a2919061056e565b6102b5565b6040516100b49190610344565b60405180910390f35b600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60006003826040516100f5919061061f565b602060405180830381855afa158015610112573d6000803e3d6000fd5b5050506040515160601b6bffffffffffffffffffffffff19169050600080828152602001908152602001600020339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060015460008083815260200190815260200160002080549050106102b15760005b6001548110156102755760008083815260200190815260200160002081815481106101ef576101ee610636565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6002549081150290604051600060405180830381858888f19350505050158015610261573d6000803e3d6000fd5b50808061026d90610694565b9150506101c1565b50600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16ff5b5050565b600060205281600052604060002081815481106102d157600080fd5b906000526020600020016000915091509054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b600061032e82610303565b9050919050565b61033e81610323565b82525050565b60006020820190506103596000830184610335565b92915050565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b6103c68261037d565b810181811067ffffffffffffffff821117156103e5576103e461038e565b5b80604052505050565b60006103f861035f565b905061040482826103bd565b919050565b600067ffffffffffffffff8211156104245761042361038e565b5b61042d8261037d565b9050602081019050919050565b82818337600083830152505050565b600061045c61045784610409565b6103ee565b90508281526020810184848401111561047857610477610378565b5b61048384828561043a565b509392505050565b600082601f8301126104a05761049f610373565b5b81356104b0848260208601610449565b91505092915050565b6000602082840312156104cf576104ce610369565b5b600082013567ffffffffffffffff8111156104ed576104ec61036e565b5b6104f98482850161048b565b91505092915050565b6000819050919050565b61051581610502565b811461052057600080fd5b50565b6000813590506105328161050c565b92915050565b6000819050919050565b61054b81610538565b811461055657600080fd5b50565b60008135905061056881610542565b92915050565b6000806040838503121561058557610584610369565b5b600061059385828601610523565b92505060206105a485828601610559565b9150509250929050565b600081519050919050565b600081905092915050565b60005b838110156105e25780820151818401526020810190506105c7565b60008484015250505050565b60006105f9826105ae565b61060381856105b9565b93506106138185602086016105c4565b80840191505092915050565b600061062b82846105ee565b915081905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b600061069f82610538565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82036106d1576106d0610665565b5b60018201905091905056fea2646970667358221220d11b498d34c527944b8940125c731cb3ac6c9e8c120b6f0e12763b1914a8615a64736f6c63430008110033";
if (args[0] == 0) {
    var constructorCodes = web3.eth.abi.encodeParameters(['uint','uint'], [args[1], 100000000]);
    console.log("constructorCodes: " + constructorCodes.substring(2));
    contract_bytes += constructorCodes.substring(2)
    call_create_contract(contract_bytes, "pkeet", "pkeet test");
}

if (args[0] == 1) {
    set_all_params();
}

if (args[0] == 2) {
    call_verify(args[1])
}
