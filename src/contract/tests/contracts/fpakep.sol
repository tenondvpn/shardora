// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.17 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract Fpakep {
    uint256 global_id;
    event DebugEvent(
       uint256 value
    );

    struct UserInfo {
        bytes32 hash;
        bytes public_key;
        bytes cipher_str;
        bool exists;
    }

    event DebugEventBytes(
       bytes value
    );

    mapping(bytes32 => UserInfo) public public_keys;
    bytes32[] all_hashes;

    function AddUserPublicKey(bytes32 hash, bytes memory public_key) public {
        emit DebugEvent(0);
        require(!public_keys[hash].exists);
        emit DebugEvent(1);
        UserInfo storage item = public_keys[hash];
        item.hash = hash;
        item.public_key = public_key;
        item.exists = true;
        emit DebugEvent(2);
        all_hashes.push(hash);
        emit DebugEvent(all_hashes.length);
    }

    function EncryptMessage(bytes32 hash, bytes memory cipher_str) public {
        emit DebugEvent(3);
        require(public_keys[hash].exists);
        public_keys[hash].cipher_str = cipher_str;
        emit DebugEvent(7);
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

    function NumberToHex(bytes memory buffer) public pure returns (bytes memory) {
        bytes memory converted = new bytes(buffer.length * 2);
        bytes memory _base = "0123456789abcdef";
        bool find_first = false;
        uint256 start_idx = 0;
        for (uint256 i = 0; i < buffer.length; i++) {
            converted[start_idx * 2] = _base[uint8(buffer[i]) / _base.length];
            converted[start_idx * 2 + 1] = _base[uint8(buffer[i]) % _base.length];
            if (find_first) {
                start_idx++;
                continue;
            }

            if (converted[start_idx * 2] != '0' || converted[start_idx * 2 + 1] != '0') {
                find_first = true;
                start_idx++;
            }
        }

        if (start_idx == 0) {
            bytes memory new_bytes_sub = new bytes(2);
            new_bytes_sub[0] = '0';
            new_bytes_sub[1] = '0';
            return new_bytes_sub;
        }


        bytes memory new_bytes = new bytes(start_idx * 2);
        for (uint256 i = 0; i < start_idx * 2; ++i) {
            new_bytes[i] = converted[i];
        }

        return new_bytes;
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

    function GetItemJson(UserInfo memory item, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"hash":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(item.hash));
        all_bytes[filedCount++] = '","cipher_str":"';
        all_bytes[filedCount++] = ToHex(item.cipher_str);
        all_bytes[filedCount++] = '","public_key":"';
        all_bytes[filedCount++] = ToHex(item.public_key);

        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllItemJson(uint256 start_pos, uint256 len) public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_hashes.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_hashes.length;
        for (uint i=start_pos; i<arrayLength && validLen <= len; i++) {
            all_bytes[i + 1] = GetItemJson(public_keys[all_hashes[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }
}
