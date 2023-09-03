// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The pledged currency can only be recovered by the seller
// 2. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 3. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract C2CSellOrder {
    struct SellOrder {
        bytes accountsReceivable;
        address payable addr;
        uint256 pledgeAmount;
        mapping(address => bool) reports;
        bool managerReleased;
        bool sellerReleased;
        bool exists;
    }

    public address owner;
    uint256 public minExchangeValue;
    uint256 public minReportCount
    mapping(address => SellOrder) public orders;
    mapping(address => bool) public valid_managers;

    constructor(address[] memory managers, uint256 minAmount, uint256 minReport) payable {
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }

        minReportCount = minReport;
        valid_managers[msg.sender] = true;
        minExchangeValue = minAmount;
        valid_managers = managers;
        owner = msg.sender;
    }

    function SetManager(address[] memory managers) public {
        require(owner == msg.sender);
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }
    }

    function NewSellOrder(bytes memory receivable) public payable {
        require(msg.value >= minExchangeValue);
        require(!orders[msg.sender].exists);
        require(!valid_managers[msg.sender].exists);
        orders[msg.sender] = SellOrder({
            accountsReceivable: receivable,
            addr: msg.sender,
            pledgeAmount: msg.value,
            reportCount: 0,
            managerReleased: false,
            sellerReleased: false,
            exists: true
        });
    }

    function ManagerReleaseForce(address seller) public payable {
        require(orders[msg.sender].exists);
        require(valid_managers[msg.sender]);
        SellOrder order = orders[seller];
        require(order.addr == seller);
        payable(order.addr).transfer(order.pledgeAmount);
        delete orders[seller];
    }

    function ManagerRelease(address seller) public payable {
        require(orders[msg.sender].exists);
        require(valid_managers[msg.sender]);
        SellOrder order = orders[seller];
        require(order.reports.length <= minReportCount);
        order.managerReleased = true;
        if (order.sellerReleased) {
            payable(order.addr).transfer(order.pledgeAmount);
            delete orders[seller];
        }
    }

    function SellerRelease() public payable {
        require(orders[msg.sender].exists);
        SellOrder order = orders[msg.sender];
        order.sellerReleased = true;
        if (order.managerReleased) {
            payable(msg.sender).transfer(order.pledgeAmount);
            delete orders[msg.sender];
        }
    }

    function Report(address seller) public {
        require(orders[seller].exists);
        require(!orders[seller].reports[msg.sender]);
        orders[seller].reports[msg.sender] = true;
    }

    function TestQuery() public view returns(bytes memory) {
        bytes memory data = '{"amount": 198734, "tmp":  "sdfasdfasdfasdfasdfadsfadsfasdfadfsdfadfadfsadfsdfasdfasdf"}';
        return data;
    }

    function TestEvent(address to, uint256 amount) public payable {
        emit NewTrade(block.timestamp, msg.sender, to, amount);
    }
}
