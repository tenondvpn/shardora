const Web3 = require('web3')
var net = require('net');
var web3 = new Web3(new Web3.providers.IpcProvider('/Users/myuser/Library/Ethereum/geth.ipc', net)); // mac os path

var giveRightToVoteCode = web3.eth.abi.encodeFunctionSignature('giveRightToVote(address)');
var giveRightToVoteParam = web3.eth.abi.encodeParameters(['address'], ['0xe70c72fcdb57df6844e4c44cd9f02435b628398c']);
console.log("giveRightToVote 0: " + giveRightToVoteCode.substring(2) + giveRightToVoteParam.substring(2));
giveRightToVoteParam = web3.eth.abi.encodeParameters(['address'], ['0xdb1e0168d3da18ec513ea4c0ebc7ca000563df68']);
console.log("giveRightToVote 1: " + giveRightToVoteCode.substring(2) + giveRightToVoteParam.substring(2));

var delegateCode = web3.eth.abi.encodeFunctionSignature('delegate(address)');
var voteCode = web3.eth.abi.encodeFunctionSignature('vote(uint256)');
var winningProposalCode = web3.eth.abi.encodeFunctionSignature('winningProposal()');
console.log("winningProposalParam: " + winningProposalCode.substring(2));
var winnerNameCode = web3.eth.abi.encodeFunctionSignature('winnerName()');
console.log("winnerNameParam: " + winnerNameCode.substring(2));
var voteParam = web3.eth.abi.encodeParameters(['uint'], [1]);
console.log("voteParam: " + voteCode.substring(2) + voteParam.substring(2));

var constructorCodes = web3.eth.abi.encodeParameters(['bytes32[] memory'], [['0x348ce564d427a3311b6536bbcff9390d69395b06ed6c486954e971d960fe8704', '0x348ce564d427a3311b6536bbcff9390d69395b06ed6c486954e971d960fe8701', '0x348ce564d427a3311b6536bbcff9390d69395b06ed6c486954e971d960fe8702']]);
console.log("constructorCodes: " + constructorCodes.substring(2));

