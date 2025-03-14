// SPDX-License-Identifier: MIT
// pragma solidity ^0.8.0;
pragma solidity >= 0.8.17 < 0.9.0;

contract DataComplianceManager {
    // 管理员地址，用于管理预言机节点列表
    address public admin;

    // 存储合法的预言机节点地址
    mapping(address => bool) public oracleNodes;

    // 存储数据哈希与合规检测结果的键值对
    mapping(bytes32 => string) public complianceResults;

    // 存储数据哈希与上链时间戳的键值对
    mapping(bytes32 => uint256) public complianceTimestamps;

    // 事件：用于记录预言机节点的添加和删除
    event OracleNodeAdded(address indexed oracleAddress);
    event OracleNodeRemoved(address indexed oracleAddress);

    // 事件：用于记录合规检测结果的存储以及合规检测结果上链的时间
    event ComplianceResultStored(bytes32 indexed dataHash, uint256 timestamp, string result);

    // 构造函数，设置管理员地址
    constructor() {
        admin = msg.sender;
    }

}