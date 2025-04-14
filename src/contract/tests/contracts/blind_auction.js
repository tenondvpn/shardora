const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var bidCode = web3.eth.abi.encodeFunctionSignature('bid(bytes32)');
var revealCode = web3.eth.abi.encodeFunctionSignature('reveal(uint256[],bool[],bytes32[])');
var withdrawCode = web3.eth.abi.encodeFunctionSignature('withdraw()');
var auctionEndCode = web3.eth.abi.encodeFunctionSignature('auctionEnd()');

// params
var contriuctParam = web3.eth.abi.encodeParameters(['uint256', 'uint256', 'address'], ['100', '100', '0x544064949151817a1185e931ea43a71493f9f33c']);
console.log("contriuctParam: " + contriuctParam);
var bidParam1 = [
   ['49800000', '59800000','29800000','79800000','39800000'],
   [false, true, false, false, true],
   ['0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93d1',
    '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93d2',
    '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93d3',
    '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93d4',
   '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93d5',]
];

for (var i = 0; i < bidParam1[0].length; ++i) {
    var blindedBid1_1 = web3.utils.keccak256(web3.eth.abi.encodeParameters(['uint256', 'bool', 'bytes32'], [bidParam1[0][i], bidParam1[1][i], bidParam1[2][i]]));
    var bidParma1_1 = web3.eth.abi.encodeParameters(['bytes32'], [blindedBid1_1]);
    console.log("bidParma: " + bidCode.substring(2) + bidParma1_1.substring(2));

}

var revealParam1 = web3.eth.abi.encodeParameters(['uint256[]', 'bool[]', 'bytes32[]'], bidParam1);
console.log("revealParam1: " + revealParam1);

var bidParam2 = [
    ['56900000', '66800000', '76800000', '66800000', '46800000'],
    [false, true, false, false, true],
    ['0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93c1',
        '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93c2',
        '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93c3',
        '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93c4',
        '0xf559642966b18c5e58a82106d7cbb6dfaa449e1820dda477580b08bab68b93c5',]
]

for (var i = 0; i < bidParam2[0].length; ++i) {
    var blindedBid1_1 = web3.utils.keccak256(web3.eth.abi.encodeParameters(['uint256', 'bool', 'bytes32'], [bidParam2[0][i], bidParam2[1][i], bidParam2[2][i]]));
    var bidParma1_1 = web3.eth.abi.encodeParameters(['bytes32'], [blindedBid1_1]);
    console.log("bidParma: " + bidCode.substring(2) + bidParma1_1.substring(2));

}

var revealParam2 = web3.eth.abi.encodeParameters(['uint256[]', 'bool[]', 'bytes32[]'], bidParam2)
console.log("revealParam2: " + revealParam2);




