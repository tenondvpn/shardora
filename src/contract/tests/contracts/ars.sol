// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract Ars {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;
    struct ArsInfo {
        uint256 ring_size;
        uint256 signer_count;
        bytes32 id;
        bytes32 res_info;
        bool exists;
    }

    event DebugEvent(
       uint256 value
    );

    mapping(bytes32 => ArsInfo) public ars_map;
    bytes32[] all_ids;

    // SetUp：初始化算法，需要用到pbc库
    constructor(bytes memory enc_init_param) {
        enc_init_param_ = ripemd160(enc_init_param);
    }

    function call_proxy_reenc(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    function CreateNewArs(uint ring_size, uint signer_count, bytes32 id, bytes memory params) public {
        emit DebugEvent(0);

        require(!ars_map[id].exists);
        emit DebugEvent(1);
        bytes32 res = ripemd160(params);
        ars_map[id] = ArsInfo({
            ring_size: ring_size,
            signer_count: signer_count,
            id: id,
            res_info: res,
            exists: true
        });
        all_ids.push(id);
        emit DebugEvent(2);
    }

    function SingleSign(bytes32 id, bytes memory params) public {
        emit DebugEvent(3);
        require(ars_map[id].exists);
        emit DebugEvent(4);
        bytes32 res = ripemd160(params);
        emit DebugEvent(5);
    }

    function AggSign(bytes32 id, bytes memory params) public {
        emit DebugEvent(6);
        require(ars_map[id].exists);
        emit DebugEvent(7);
        bytes32 res = ripemd160(params);
        emit DebugEvent(8);
    }

    function bytesConcat(bytes[] memory arr, uint count) public pure returns (bytes memory){
        uint len = 0;
        for (uint i = 0; i < count; i++) {
            len += arr[i].length;
        }

        bytes memory bret = new bytes(len);
        uint k = 0;
        for (uint i = 0; i < count; i++) {
            for (uint j = 0; j < arr[i].length; j++) {
                bret[k++] = arr[i][j];
            }
        }

        return bret;
    }

    function ToHex(bytes memory buffer) public pure returns (bytes memory) {
        bytes memory converted = new bytes(buffer.length * 2);
        bytes memory _base = "0123456789abcdef";
        for (uint256 i = 0; i < buffer.length; i++) {
            converted[i * 2] = _base[uint8(buffer[i]) / _base.length];
            converted[i * 2 + 1] = _base[uint8(buffer[i]) % _base.length];
        }

        return converted;
    }

    function toBytes(address a) public pure returns (bytes memory) {
        return abi.encodePacked(a);
    }

    function u256ToBytes(uint256 x) public pure returns (bytes memory b) {
        b = new bytes(32);
        assembly { mstore(add(b, 32), x) }
    }

    function Bytes32toBytes(bytes32 _data) public pure returns (bytes) {
        return abi.encodePacked(_data);
    }

    function GetArsJson(ArsInfo memory ars, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"ring_size":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(ars.ring_size));
        all_bytes[filedCount++] = '","signer_count":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(ars.signer_count));
        all_bytes[filedCount++] = '","id":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(ars.id));
        all_bytes[filedCount++] = '","res":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(ars.res));
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllArsJson() public view returns(bytes memory) {
        uint validLen = 0;
        bytes[] memory all_bytes = new bytes[](validLen + 2);
        all_bytes[0] = '[';
        uint arrayLength = ars_map.length;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetArsJson(ars_map[all_ids[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }
}
