// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;


contract Phr {
    address[] public valid_aas;
    address public owner;

    struct RidInfo {
        bytes pk;
        bytes ci;
        bool exists;
    }

    mapping(bytes32 => RidInfo) public rids;
    mapping(bytes => mapping(bytes32 => bool)) public attrs;

    constructor(address[] memory aas) {
        valid_aas = aas;
        owner = msg.sender;
    }

    function ResAdd(bytes32 rid, bytes memory pkDo, bytes memory ci) public {
        require(owner == msg.sender);
        require(!rids[rid].exists);
        rids[rid] = RidInfo({
            pk: pkDo,
            ci: ci,
            exists: true
        });
    }

    function AttrReg(bytes memory pk, bytes32 attr_hash, bytes[] memory sigs) public {
        require(valid_aas.length == sigs.length);
        require(attrs[pk][attr_hash] == false);
        for (uint i = 0; i < sigs.length; i++) {
            require(recoverSigner(prefixed(attr_hash), sigs[i]) == valid_aas[i]);
        }

        attrs[pk][attr_hash] = true;
    }

    function UpdateAttr(bytes memory pk, bytes32 attr_hash, bytes[] memory sigs) public {
        require(valid_aas.length == sigs.length);
        for (uint i = 0; i < sigs.length; i++) {
            require(recoverSigner(prefixed(attr_hash), sigs[i]) == valid_aas[i]);
        }

        attrs[pk][attr_hash] = true;
    }

    function QuerryAttr(bytes memory pk, bytes32 attr_hash) public return (bool) {
        return attrs[pk][attr_hash];
    }

    function splitSignature(bytes memory sig)
        internal
        pure
        returns (uint8 v, bytes32 r, bytes32 s) {
        require(sig.length == 65);

        assembly {
            r := mload(add(sig, 32))
            s := mload(add(sig, 64))
            v := byte(0, mload(add(sig, 96)))
        }

        return (v, r, s);
    }

    function recoverSigner(bytes32 message, bytes memory sig)
        internal
        pure
        returns (address) {
        (uint8 v, bytes32 r, bytes32 s) = splitSignature(sig);
        return ecrecover(message, v, r, s);
    }

    /// 加入一个前缀，因为在eth_sign签名的时候会加上。
    function prefixed(bytes32 hash) internal pure returns (bytes32) {
        return keccak256(abi.encodePacked("\x19Ethereum Signed Message:\n32", hash));
    }
}
