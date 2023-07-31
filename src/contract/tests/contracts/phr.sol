// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

contract Phr {
    address[] public valid_aas;
    address payable public owner;

    struct RidInfo {
        bytes pk;
        bytes ci;
        bool exists;
    }

    struct PidInfo {
        bytes32 rid;
        bool exists;
    }

    struct PolicyInfo {
        bytes32 attr_hash;
        uint256 time;
        bool exists;
    }

    mapping(bytes32 => RidInfo) public rids;
    mapping(bytes => mapping(bytes32 => bool)) public pk_attrs;
    mapping(bytes32 => mapping(bytes => bool)) public attr_pks;
    mapping(bytes32 => PolicyInfo[]) public rid_attrs;
    mapping(bytes32 => PidInfo) public pids;

    constructor(address[] memory aas) {
        valid_aas = aas;
        owner = payable(msg.sender);
    }

    function Recover() public payable {
        selfdestruct(owner);
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

    function QuerryRes(bytes32 rid) public returns (bool) {
        return rids[rid].exists;
    }

    function AttrReg(bytes memory pk, bytes32 attr_hash, bytes[] memory sigs) public {
        require(valid_aas.length == sigs.length);
        require(pk_attrs[pk][attr_hash] == false);
        for (uint i = 0; i < sigs.length; i++) {
            require(recoverSigner(prefixed(attr_hash), sigs[i]) == valid_aas[i]);
        }

        pk_attrs[pk][attr_hash] = true;
        attr_pks[attr_hash][pk] = true;
    }

    function UpdateAttr(bytes memory pk, bytes32 attr_hash, bytes[] memory sigs) public {
        require(valid_aas.length == sigs.length);
        for (uint i = 0; i < sigs.length; i++) {
            require(recoverSigner(prefixed(attr_hash), sigs[i]) == valid_aas[i]);
        }

        pk_attrs[pk][attr_hash] = true;
        attr_pks[attr_hash][pk] = true;
    }

    function QuerryAttr(bytes memory pk, bytes32 attr_hash) public returns (bool) {
        return pk_attrs[pk][attr_hash];
    }

    function PolicyAdd(bytes32 pid, bytes32 rid, bytes32[] memory attr_hash, uint256[] memory timeout) public {
        require(owner == msg.sender);
        require(!pids[pid].exists);
        require(attr_hash.length == timeout.length);
        pids[pid] = PidInfo({
            rid: rid,
            exists: true
        });

        uint arrayLength = attr_hash.length;
        for (uint i=0; i<arrayLength; i++) {
            rid_attrs[rid].push(PolicyInfo({
                attr_hash: attr_hash[i],
                time:timeout[i],
                exists: true
            }));
        }
    }

    function PolicyQry(bytes32 pid) public returns (bool) {
        require(owner == msg.sender);
        return pids[pid].exists;
    }

    function Access(bytes memory pk, bytes32 rid) public returns (bool) {
        uint arrayLength = rid_attrs[rid].length;
        require(arrayLength > 0);
        for (uint i=0; i<arrayLength; i++) {
            if (attr_pks[rid_attrs[rid][i].attr_hash][pk] && rid_attrs[rid][i].time >= block.timestamp) {
                return true;
            }
        }

        return false;
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
