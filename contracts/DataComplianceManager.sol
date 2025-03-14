// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

contract DataComplianceManager {
    // 管理员地址，用于管理预言机节点列表
    address public admin;

    // 存储合法的预言机节点地址
    mapping(address => bool) public oracleNodes;

    // 存储数据哈希与合规检测结果的键值对
    mapping(bytes32 => string) public complianceResults;

    // 存储数据哈希与上链时间戳的键值对
    mapping(bytes32 => uint256) public complianceTimestamps;

    // 构造函数，设置管理员地址
    constructor() {
        admin = msg.sender;
    }

}