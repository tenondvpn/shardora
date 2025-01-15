// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract Pki {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;
    struct PkiInfo {
        uint256 pki_count;
        uint256 ib_count;
        bytes32 id;
        bool exists;
    }

    event DebugEvent(
       uint256 value
    );

    event DebugEventBytes(
       bytes value
    );

    mapping(bytes32 => PkiInfo) public pki_map;
    bytes32[] all_ids;

    function PkiExtract(uint256 pki_count, uint256 ib_count, bytes32 id, bytes memory params) public {
        emit DebugEvent(0);
        if (!pki_map[id].exists) {
            emit DebugEvent(1);
            pki_map[id] = PkiInfo({
                pki_count: pki_count,
                ib_count: ib_count,
                id: id,
                exists: true
            });
        }

        emit DebugEvent(2);
        bytes32 res = ripemd160(params);
        all_ids.push(id);
        emit DebugEvent(all_ids.length);
    }

    function IbExtract(bytes32 id, bytes memory params) public {
        emit DebugEvent(3);
        require(pki_map[id].exists);
        emit DebugEvent(4);
        bytes32 res = ripemd160(params);
        emit DebugEvent(5);
    }

    function EncKeyGen(bytes32 id, bytes memory params) public {
        emit DebugEvent(6);
        require(pki_map[id].exists);
        emit DebugEvent(7);
        bytes32 res = ripemd160(params);
        emit DebugEvent(8);
    }

    function DecKeyGen(bytes32 id, bytes memory params) public {
        emit DebugEvent(9);
        require(pki_map[id].exists);
        emit DebugEvent(10);
        bytes32 res = ripemd160(params);
        emit DebugEvent(11);
    }

    function Enc(bytes32 id, bytes memory params) public {
        emit DebugEvent(12);
        require(pki_map[id].exists);
        emit DebugEvent(13);
        bytes32 res = ripemd160(params);
        emit DebugEvent(14);
    }

    function Dec(bytes32 id, bytes memory params) public {
        emit DebugEvent(15);
        require(pki_map[id].exists);
        emit DebugEvent(16);
        bytes32 res = ripemd160(params);
        emit DebugEvent(17);
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

    function Bytes32toBytes(bytes32 _data) public pure returns (bytes memory) {
        return abi.encodePacked(_data);
    }

    function GetPkiJson(PkiInfo memory pki, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"pki_count":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(pki.pki_count));
        all_bytes[filedCount++] = '","ib_count":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(pki.ib_count));
        all_bytes[filedCount++] = '","id":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(pki.id));
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllPkiJson() public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_ids.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_ids.length;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetPkiJson(pki_map[all_ids[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }
}
