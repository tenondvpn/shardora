// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract C2CSellOrder {
    struct SellOrder {
        bytes accountsReceivable;
        address payable addr;
        uint256 pledgeAmount;
        uint256 price;
        bool managerReleased;
        bool sellerReleased;
        bool exists;
        bool reported;
        uint256 orderId;
        uint256 height;
        address buyer;
        uint256 amount;
    }

    event NewSellout(
       address from,
       bytes receivable,
       uint256 price,
       uint256 pledgeAmount,
       uint256 orderId
       );
    event NewSelloutValue(
       uint256 value
       );
    event NewSelloutLength(
       uint256 value
       );

    uint256 orderId;
    address public owner;
    uint256 public minPlegementValue;
    uint256 public minExchangeValue;
    mapping(address => SellOrder) public orders;
    mapping(address => bool) public valid_managers;
    address[] all_sellers;

    constructor(address[] memory managers, uint256 minPlegement, uint256 minAmount) payable {
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }

        orderId = 0;
        valid_managers[msg.sender] = true;
        minPlegementValue = minPlegement;
        minExchangeValue = minAmount;
        owner = msg.sender;
    }

    function SetManager(address[] memory managers) public {
        require(owner == msg.sender);
        require(!orders[msg.sender].exists);
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }
    }

    function NewSellOrder(bytes memory receivable, uint256 price) public payable {
        emit NewSelloutValue(msg.value);
        require(msg.value >= minPlegementValue);
        emit NewSellout(msg.sender, receivable, price, msg.value, orderId);

        if (orders[msg.sender].exists) {
            require(orders[msg.sender].managerReleased);
        }

        emit NewSellout(msg.sender, receivable, price, msg.value, orderId);

        require(!valid_managers[msg.sender]);
        emit NewSellout(msg.sender, receivable, price, msg.value, orderId);

        orders[msg.sender] = SellOrder({
            accountsReceivable: receivable,
            addr: payable(msg.sender),
            pledgeAmount: msg.value,
            price: price,
            managerReleased: false,
            sellerReleased: false,
            exists: true,
            reported: false,
            orderId: orderId,
            height: block.number,
            buyer:msg.sender,
            amount:0
        });

        all_sellers.push(msg.sender);
        emit NewSelloutLength(all_sellers.length);
        emit NewSellout(msg.sender, receivable, price, msg.value, orderId);
        orderId++;
    }

    function Confirm(address payable buyer, uint256 amount) public payable {
        emit NewSelloutValue(amount);
        emit NewSelloutValue(minExchangeValue);

        require(amount >= minExchangeValue);
        require(orders[msg.sender].exists);
        require(!orders[msg.sender].managerReleased);
        require(!orders[msg.sender].sellerReleased);
        require(!orders[msg.sender].reported);
        emit NewSelloutValue(orders[msg.sender].pledgeAmount);

        require(orders[msg.sender].pledgeAmount >= amount);
        SellOrder memory order = orders[msg.sender];
        order.pledgeAmount -= amount;
        order.height = block.number;
        order.buyer = buyer;
        order.amount = amount;
        payable(buyer).transfer(amount);
        if (order.pledgeAmount < minExchangeValue) {
            if (order.pledgeAmount > 0) {
                payable(msg.sender).transfer(order.pledgeAmount);
            }

            order.pledgeAmount = 0;
            uint seller_len = all_sellers.length;
            for (uint i = 0; i < seller_len; ++i) {
                if (all_sellers[i] == msg.sender) {
                    delete all_sellers[i];
                    break;
                }
            }

            delete orders[msg.sender];
        } else {
            orders[msg.sender] = order;
        }

        emit NewSelloutValue(order.pledgeAmount);
    }

    function ManagerReleaseForce(address seller) public payable {
        require(orders[seller].exists);
        require(valid_managers[msg.sender]);
        SellOrder memory order = orders[seller];
        require(order.addr == seller);
        require(order.managerReleased);
        uint seller_len = all_sellers.length;
        for (uint i = 0; i < seller_len; ++i) {
            if (all_sellers[i] == seller) {
                delete all_sellers[i];
                break;
            }
        }

        delete orders[seller];
    }

    function ManagerRelease(address seller) public payable {
        emit NewSelloutValue(1);

        require(orders[seller].exists);
        emit NewSelloutValue(2);

        require(valid_managers[msg.sender]);
        emit NewSelloutValue(3);

        SellOrder memory order = orders[seller];
        emit NewSelloutValue(4);

        require(order.addr == seller);
        emit NewSelloutValue(5);

        require(!order.managerReleased);
        emit NewSelloutValue(6);

        order.managerReleased = true;
        order.height = block.number;
        if (order.pledgeAmount > 0) {
            emit NewSelloutValue(address(this).balance);
            emit NewSelloutValue(order.pledgeAmount);
            payable(order.addr).transfer(order.pledgeAmount);
            order.pledgeAmount = 0;
            emit NewSelloutValue(7);

        }
            
        emit NewSelloutValue(8);

        orders[seller] = order;
        emit NewSelloutValue(9);

    }

    function SellerRelease() public payable {
        require(orders[msg.sender].exists);
        SellOrder memory order = orders[msg.sender];
        order.sellerReleased = true;
        order.height = block.number;
        if (order.managerReleased) {
            payable(msg.sender).transfer(order.pledgeAmount);
            uint seller_len = all_sellers.length;
            for (uint i = 0; i < seller_len; ++i) {
                if (all_sellers[i] == msg.sender) {
                    delete all_sellers[i];
                    break;
                }
            }
            delete orders[msg.sender];
        } else {
            orders[msg.sender] = order;
        }
    }

    function Report(address seller) public {
        require(orders[seller].exists);
        require(!orders[seller].reported);
        orders[seller].reported = true;
        orders[seller].height = block.number;
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

    function GetOrderJson(SellOrder memory order, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"r":"';
        all_bytes[filedCount++] = ToHex(order.accountsReceivable);
        all_bytes[filedCount++] = '","a":"';
        all_bytes[filedCount++] = ToHex(toBytes(order.addr));
        all_bytes[filedCount++] = '","b":"';
        all_bytes[filedCount++] = ToHex(toBytes(order.buyer));
        all_bytes[filedCount++] = '","m":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(order.pledgeAmount));
        all_bytes[filedCount++] = '","p":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(order.price));
        all_bytes[filedCount++] = '","h":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(order.height));
        all_bytes[filedCount++] = '","bm":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(order.amount));
        bytes memory mr = 'false';
        if (order.managerReleased) {
            mr = 'true';
        }

        bytes memory sr = 'false';
        if (order.sellerReleased) {
            sr = 'true';
        }

        bytes memory rp = 'false';
        if (order.reported) {
            rp = 'true';
        }

        all_bytes[filedCount++] = '","mr":';
        all_bytes[filedCount++] = mr;
        all_bytes[filedCount++] = ',"sr":';
        all_bytes[filedCount++] = sr;
        all_bytes[filedCount++] = ',"rp":';
        all_bytes[filedCount++] = rp;
        all_bytes[filedCount++] = ',"o":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(order.orderId));
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetOrdersJson() public view returns(bytes memory) {
        bytes[] memory all_bytes = new bytes[](all_sellers.length + 2);
        all_bytes[0] = '[';
        uint arrayLength = all_sellers.length;
        uint validLen = 1;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetOrderJson(orders[all_sellers[i]], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }
}
