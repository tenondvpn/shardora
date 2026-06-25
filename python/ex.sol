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
        bytes memory key = abi.encodePacked(hash, msg.sender);
        require(!purchase_map[key]);
        emit DebugEvent(6);
        emit DebugEvent(item_map[hash].price);
        emit DebugEvent(msg.value);
        ItemInfo storage item = item_map[hash];
        require(item_map[hash].price <= msg.value);
        emit DebugEvent(7);
        item.buyers.push(BuyerInfo(payable(msg.sender), msg.value, time));
        purchase_map[key] = true;
        emit DebugEvent(99999900000000000 + msg.value);
    }

    function ConfirmPurchase(bytes32 hash) public payable {
        emit DebugEvent(8);
        require(item_map[hash].exists);
        emit DebugEvent(9);
        require(item_map[hash].owner == msg.sender);
        emit DebugEvent(10);
        require(item_map[hash].selled == 0);
        emit DebugEvent(11);
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
        emit DebugEvent(12);
        item.selled = 1;
        item.selled_price = max_price;
        item.buyer = max_buyer;
        (bool ok1,) = payable(msg.sender).call{value: max_price}("");
        require(ok1, "transfer to seller failed");
        for (uint256 i = 0; i < item.buyers.length; ++i) {
            if (item.buyers[i].buyer != max_buyer) {
                (bool ok2,) = payable(item.buyers[i].buyer).call{value: item.buyers[i].price}("");
                require(ok2, "refund failed");
            }
        }
        emit DebugEvent(13);
    }

    // ── State queries ─────────────────────────────────────────────────────

    /// @notice Total number of items ever listed.
    function TotalItems() public view returns (uint256) {
        return all_hashes.length;
    }

    /// @notice Whether an item with the given hash exists.
    function ItemExists(bytes32 hash) public view returns (bool) {
        return item_map[hash].exists;
    }

    /// @notice Sell status of an item: 0 = unsold, 1 = sold.
    function SellStatus(bytes32 hash) public view returns (uint256) {
        require(item_map[hash].exists, "not found");
        return item_map[hash].selled;
    }

    /// @notice Winning buyer and final price for a sold item.
    function SellResult(bytes32 hash) public view returns (address buyer, uint256 price) {
        require(item_map[hash].exists, "not found");
        require(item_map[hash].selled == 1, "not sold");
        return (item_map[hash].buyer, item_map[hash].selled_price);
    }

    /// @notice Number of bids placed on an item (placeholder at index 0 excluded).
    function BuyerCount(bytes32 hash) public view returns (uint256) {
        require(item_map[hash].exists, "not found");
        uint256 len = item_map[hash].buyers.length;
        return len > 0 ? len - 1 : 0;
    }

    /// @notice Whether a given address has already bid on an item.
    function HasPurchased(bytes32 hash, address buyer) public view returns (bool) {
        bytes memory key = abi.encodePacked(hash, buyer);
        return purchase_map[key];
    }

    /// @notice Number of items owned by an address.
    function OwnerItemCount(address owner) public view returns (uint256) {
        return owner_with_hash_map[owner].length;
    }

    /// @notice Hash at a given index in the global list.
    function HashAt(uint256 index) public view returns (bytes32) {
        require(index < all_hashes.length, "out of range");
        return all_hashes[index];
    }

    /// @notice Hash at a given index in an owner's item list.
    function OwnerHashAt(address owner, uint256 index) public view returns (bytes32) {
        require(index < owner_with_hash_map[owner].length, "out of range");
        return owner_with_hash_map[owner][index];
    }

    /// @notice Core fields of an item: owner, price, start/end time, sell status.
    function GetItem(bytes32 hash) public view returns (
        address owner,
        uint256 price,
        uint256 start_time_ms,
        uint256 end_time_ms,
        uint256 selled,
        uint256 selled_price,
        address buyer
    ) {
        require(item_map[hash].exists, "not found");
        ItemInfo storage item = item_map[hash];
        return (item.owner, item.price, item.start_time_ms, item.end_time_ms,
                item.selled, item.selled_price, item.buyer);
    }
}