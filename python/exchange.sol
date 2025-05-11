// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.17 <0.9.0;

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
        uint256 time;
    }
    
    struct ItemInfo {
        uint256 id;
        bytes32 hash;
        address payable owner;
        bytes info;
        uint256 price;
        uint256 start_time_ms;
        uint256 end_time_ms;
        address payable buyer;
        uint256 selled_price;
        uint256 selled;
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
    mapping(uint256 => bytes32) public id_with_hash_map;
    mapping(bytes => bool) public purchase_map;
    mapping(address => bytes32[]) public owner_with_hash_map;
    bytes32[] all_hashes;

    function CreateNewItem(bytes32 hash, bytes memory info, uint256 price, uint256 start, uint256 end) public payable {
        require(!item_map[hash].exists);
        ItemInfo storage item = item_map[hash];
        item.id = global_id++;
        item.hash = hash;
        item.owner = payable(msg.sender);
        item.info = info;
        item.price = price;
        item.start_time_ms = start;
        item.end_time_ms = end;
        item.selled = 0;
        item.selled_price = 0;
        item.buyer = payable(0x0000000000000000000000000000000000000000);
        item.buyers.push(BuyerInfo(payable(0x0000000000000000000000000000000000000000), 0, 0));
        item.exists = true;
        all_hashes.push(hash);
        id_with_hash_map[item.id] = hash;
        owner_with_hash_map[msg.sender].push(hash);
    }

    function PurchaseItem(bytes32 hash, uint256 time) public payable {
        require(item_map[hash].exists);
        require(item_map[hash].owner != msg.sender);
        emit DebugEvent(5);
        bytes[] memory all_bytes = new bytes[](2);
        all_bytes[0] = Bytes32toBytes(hash);
        all_bytes[1] = toBytes(msg.sender);
        bytes memory key = bytesConcat(all_bytes, 2);
        require(!purchase_map[key]);
        emit DebugEvent(6);
        ItemInfo storage item = item_map[hash];
        require(item_map[hash].price <= msg.value);
        emit DebugEvent(7);
        item.buyers.push(BuyerInfo(payable(msg.sender), msg.value, time));
        purchase_map[key] = true;
        emit DebugEvent(99999900000000000 + msg.value);
    }

    function ConfirmPurchase(bytes32 hash) public payable {
        require(item_map[hash].exists);
        require(item_map[hash].owner == msg.sender);
        require(item_map[hash].selled == 0);
        ItemInfo storage item = item_map[hash];
        uint256 max_price = item.buyers[0].price;
        address payable max_buyer = item.buyers[0].buyer;
        for (uint256 i = 1; i < item.buyers.length; ++i) {
            if (item.buyers[i].price > max_price) {
                max_price = item.buyers[i].price;
                max_buyer = item.buyers[i].buyer;
            }
        }

        require(max_price >= item.price);
        item.selled = 1;
        item.selled_price = max_price;
        item.buyer = max_buyer;
        payable(msg.sender).transfer(max_price);
        for (uint256 i = 0; i < item.buyers.length; ++i) {
            if (item.buyers[i].buyer != max_buyer) {
                payable(item.buyers[i].buyer).transfer(item.buyers[i].price);
            }
        }
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
            bytes memory new_bytes = new bytes(2);
            new_bytes[0] = '0';
            new_bytes[1] = '0';
            return new_bytes;
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

    function GetBuyerJson(BuyerInfo memory item, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"buyer":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.buyer));
        all_bytes[filedCount++] = '","price":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.price));
        all_bytes[filedCount++] = '","time":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.time));

        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetSubArrayItem(BuyerInfo[] memory buyers) public pure returns (bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](buyers.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = buyers.length-1;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetBuyerJson(buyers[i+1], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }

    function GetItemJson(ItemInfo memory item, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"id":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.id));
        all_bytes[filedCount++] = '","hash":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(item.hash));
        all_bytes[filedCount++] = '","owner":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.owner));
        all_bytes[filedCount++] = '","info":"';
        all_bytes[filedCount++] = ToHex(item.info);
        all_bytes[filedCount++] = '","selled_price":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.selled_price));
        all_bytes[filedCount++] = '","selled":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.selled));
        all_bytes[filedCount++] = '","buyer":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.buyer));
        all_bytes[filedCount++] = '","price":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.price));
        all_bytes[filedCount++] = '","start_time":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.start_time_ms));
        all_bytes[filedCount++] = '","end_time":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.end_time_ms));
        all_bytes[filedCount++] = '","buyers":';
        all_bytes[filedCount++] = GetSubArrayItem(item.buyers);

        if (last) {
            all_bytes[filedCount++] = '}';
        } else {
            all_bytes[filedCount++] = '},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllItemJson(uint256 start_pos, uint256 len) public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_hashes.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_hashes.length;
        uint start_idx = 0;
        for (uint i=start_pos; i<arrayLength && validLen <= len; i++) {
            all_bytes[start_idx + 1] = GetItemJson(item_map[all_hashes[i]], (i == arrayLength - 1 || validLen == len));
            ++validLen;
            ++start_idx;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }

    function GetOwnerItemJson(uint256 start_pos, uint256 len, address owner) public view returns(bytes memory) {
        uint validLen = 1;
        bytes32[] memory hash_array = owner_with_hash_map[owner];
        bytes[] memory all_bytes = new bytes[](hash_array.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = hash_array.length;
        uint start_idx = 0;
        for (uint i=start_pos; i<arrayLength && validLen <= len; i++) {
            all_bytes[start_idx + 1] = GetItemJson(item_map[hash_array[i]], (i == arrayLength - 1 || validLen == len));
            ++validLen;
            ++start_idx;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }

    function GetSellDetail(bytes32 hash) public view returns(bytes memory) {
        uint validLen = 1;
        bytes[] memory all_bytes = new bytes[](all_hashes.length + 2);
        all_bytes[0] = GetItemJson(item_map[hash], true);
        return bytesConcat(all_bytes, validLen + 1);
    }
}
