// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract Exchange {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;
    uint256 global_id;
    struct BuyerInfo {
        address payable buyer;
        uint256 price;
    }
    
    struct ItemInfo {
        uint256 id;
        bytes32 hash;
        address payable owner;
        bytes info;
        uint256 price;
        uint256 start_time_ms;
        uint256 end_time_ms;
        bool selled;
        address payable buyer;
        BuyerInfo[] buyers;
        bool exists;
    }

    event DebugEvent(
       uint256 value
    );

    event DebugEventBytes(
       bytes value
    );

    mapping(bytes32 => ItemInfo) public item_map;
    bytes32[] all_hashes;

    function CreateNewItem(bytes32 hash, bytes memory info, uint256 price, uint256 start, uint256 end) public payable {
        emit DebugEvent(0);
        require(!item_map[hash].exists);
        emit DebugEvent(1);
        ItemInfo storage item = item_map[hash];
        item.id = global_id++;
        item.hash = hash;
        item.owner = payable(msg.sender);
        item.info = info;
        item.price = price;
        item.start_time_ms = start;
        item.end_time_ms = end;
        item.selled = false;
        item.buyer = payable(0x0000000000000000000000000000000000000000);
        item.buyers = new BuyerInfo[](0);
        item.exists = true;
       

        emit DebugEvent(2);
        all_hashes.push(hash);
        emit DebugEvent(all_hashes.length);
    }

    function PurchaseItem(bytes32 hash) public payable {
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

    function GetItemJson(ItemInfo memory item, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"id":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.id));
        all_bytes[filedCount++] = '","hash":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(item.hash));
        all_bytes[filedCount++] = '","owner":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.owner));
        all_bytes[filedCount++] = '","info":"';
        all_bytes[filedCount++] = ToHex(item.info);
        all_bytes[filedCount++] = '","price":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.price));
        all_bytes[filedCount++] = '","start_time":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.start_time_ms));
        all_bytes[filedCount++] = '","end_time":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.end_time_ms));
        all_bytes[filedCount++] = '","buyer":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.buyer));

        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllItemJson() public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_hashes.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_hashes.length;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetItemJson(item_map[all_hashes[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }
}
