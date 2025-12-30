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

    // 存储用户合约地址与回调函数选择器的键值对
    mapping(address => bytes4) public userCallbacks;

    // 事件：用于记录预言机节点的添加和删除
    event OracleNodeAdded(address indexed oracleAddress);
    event OracleNodeRemoved(address indexed oracleAddress);

    // 事件：用于记录合规检测结果的存储以及合规检测结果上链的时间
    event ComplianceResultStored(bytes32 indexed dataHash, uint256 timestamp, string result);

    // 事件：有用户合约请求了合规检测
    event ComplianceCheckRequested(address indexed userContract, bytes32 indexed dataHash);

    // 构造函数，设置管理员地址
    constructor() {
        admin = msg.sender;
    }

    // 修饰器：限制只有管理员可以调用
    modifier onlyAdmin() {
        require(msg.sender == admin, "Not an admin");
        _;
    }

    // 修饰器：限制只有合法的预言机节点可以调用
    modifier onlyOracle() {
        require(oracleNodes[msg.sender], "Not an authorized oracle node");
        _;
    }

    // 添加合法的预言机节点地址
    function addOracleNode(address oracleAddress) external onlyAdmin {
        require(!oracleNodes[oracleAddress], "Oracle node already exists");
        oracleNodes[oracleAddress] = true;
        emit OracleNodeAdded(oracleAddress);
    }

    // 批量添加合法的预言机节点地址
    function addOracleNodes(address[] memory oracleAddresses) external onlyAdmin {
        for (uint256 i = 0; i < oracleAddresses.length; i++) {
            require(!oracleNodes[oracleAddresses[i]], "Oracle node already exists");
            oracleNodes[oracleAddresses[i]] = true;
            emit OracleNodeAdded(oracleAddresses[i]);
        }
    }

    // 删除合法的预言机节点地址
    function removeOracleNode(address oracleAddress) external onlyAdmin {
        require(oracleNodes[oracleAddress], "Oracle node does not exist");
        oracleNodes[oracleAddress] = false;
        emit OracleNodeRemoved(oracleAddress);
    }

    // 批量删除合法的预言机节点地址
    function removeOracleNodes(address[] memory oracleAddresses) external onlyAdmin {
        for (uint256 i = 0; i < oracleAddresses.length; i++) {
            require(oracleNodes[oracleAddresses[i]], "Oracle node does not exist");
            oracleNodes[oracleAddresses[i]] = false;
            emit OracleNodeRemoved(oracleAddresses[i]);
        }
    }

    // 用户合约发起合规检测请求，同时注册回调函数选择器
    function requestComplianceCheck(bytes32 dataHash, bytes4 callbackSelector) external {
        require(dataHash != bytes32(0), "Invalid data hash");
        require(callbackSelector != 0x0, "Invalid selector");

        userCallbacks[msg.sender] = callbackSelector;

        emit ComplianceCheckRequested(msg.sender, dataHash);
    }

    // 预言机节点利用此函数写入合规检测的结果并且回调用户合约
    function fulfillComplianceCheck(
        address userContract,
        bytes32 dataHash,
        string memory result
    ) external onlyOracle {
        require(userContract != address(0), "Invalid user contract");
        require(dataHash != bytes32(0), "Invalid data hash");
        require(bytes(result).length > 0, "Empty result");

        complianceResults[dataHash] = result;
        complianceTimestamps[dataHash] = block.timestamp;
        emit ComplianceResultStored(dataHash, block.timestamp, result);

        bytes4 selector = userCallbacks[userContract];
        require(selector != 0x0, "Callback not registered");

        (bool success, ) = userContract.call(
            abi.encodeWithSelector(selector, dataHash, result)
        );
        require(success, "Callback to user contract failed");
    }

    // 接收预言机节点发送的数据哈希和合规检测结果
    function storeComplianceResult(bytes32 dataHash, string memory result) external onlyOracle {
        require(dataHash != bytes32(0), "Invalid data hash");
        require(bytes(result).length > 0, "Invalid compliance result");

        // 存储合规检测结果
        complianceResults[dataHash] = result;
        complianceTimestamps[dataHash] = block.timestamp;
        emit ComplianceResultStored(dataHash, block.timestamp, result);
    }

    // 查询合规检测结果
    function getComplianceResult(bytes32 dataHash) external view returns (string memory) {
        require(dataHash != bytes32(0), "Invalid data hash");
        require(bytes(complianceResults[dataHash]).length > 0, "Compliance result not found");

        return complianceResults[dataHash];
    }

    // 查询合规检测结果的上链时间戳
    function getComplianceTimestamp(bytes32 dataHash) external view returns (uint256) {
        require(dataHash != bytes32(0), "Invalid data hash");
        require(complianceTimestamps[dataHash] > 0, "Compliance timestamp not found");

        return complianceTimestamps[dataHash];
    }

    // 转移管理员权限
    function transferAdmin(address newAdmin) external onlyAdmin {
        require(newAdmin != address(0), "Invalid new admin address");
        admin = newAdmin;
    }
}