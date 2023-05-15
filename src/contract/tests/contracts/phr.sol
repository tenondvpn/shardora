// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;


contract Phr {
    address[] public valid_aas;

    struct RidInfo {
        bytes pk;
        bytes ci;
    }

    mapping(uint256 => RidInfo) public rids;
    mapping(bytes => bytes32[]) public attrs;

    constructor(address[] memory aas) {
        valid_aas = aas;
    }

    function ResAdd(uint256 rid, bytes memory pkDo, bytes memory ci) public {
        rids[rid] = RidInfo({
            pk: pkDo,
            ci: ci
        });
    }

    function AttrReg(bytes memory pk, bytes32 attr_hash, bytes[] memory sigs) public {
        require (valid_aas.length == sigs.length);
        for (uint i = 0; i < sigs.length; i++) {
            require(recoverSigner(attr_hash, sigs[i]) == valid_aas[i]);
        }

        attrs[pk].push(attr_hash);
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
}
