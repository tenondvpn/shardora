// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.17 <0.9.0;
pragma experimental ABIEncoderV2;
//DID合约
contract mm {
    // 定义资产数据结构
    struct AssetData {
        string assetId;
        uint8 rightType;
    }

    // DID文档的数据结构
    struct DIDDocument {
        string userType;
        string created;
        string updated;
        string publicKey1;
        string publicKey2;
        string signatureValue;
        mapping(string => AssetData) assets; // 使用映射替代数组
        string[] assetIds; // 存储资产ID以便遍历
    }

    mapping(bytes32 => DIDDocument) private didDocuments;
    mapping(bytes32 => uint256) private balances; // 将 DID 哈希映射到余额
    string[] private dids;  // 存储所有的 DID
    mapping(bytes32 => uint256) private assetCounts; // 记录每个 DID 对应的资产数量


    event DIDRegistered(string did, string userType, string created);
    event DIDUpdated(string did, string updated);
    event DIDDeleted(string did);
    event TestDIDDeleted(uint256 tm);
    event AssetAssociated(string did, string assetId, uint8 rightType);
    event AssetDeleted(string did, string assetId);
    event BalanceUpdated(string did, uint256 newBalance);
    event AssetRightTypeUpdated(string did, string assetId, uint8 newRightType);


    // 注册 DID，不包含资产
    function registerDID(
        string memory _did,
        string memory _userType,
        string memory _publicKey1,
        string memory _publicKey2,
        string memory _signatureValue
    ) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        emit DIDDeleted("0000");
        require(bytes(didDocuments[didHash].created).length == 0, "DID already exists");

        emit DIDDeleted("1111");
        emit TestDIDDeleted(block.timestamp);
        emit DIDDeleted("111100001111");
        string memory currentTime = uint2str(block.timestamp);
        emit DIDDeleted("2222");
        DIDDocument storage newDocument = didDocuments[didHash];
        emit DIDDeleted("3333");
        newDocument.userType = _userType;
        newDocument.created = currentTime;
        newDocument.updated = currentTime;
        newDocument.publicKey1 = _publicKey1;
        newDocument.publicKey2 = _publicKey2;
        newDocument.signatureValue = _signatureValue;

        emit DIDDeleted("4444");
        dids.push(_did);  // 将 DID 存入列表中
        emit DIDDeleted("5555");
        emit DIDRegistered(_did, _userType, currentTime);
    }

    function deposit(string memory _did, uint256 amount) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        require(bytes(didDocuments[didHash].created).length > 0, "DID does not exist");

        balances[didHash] += amount; // 增加余额

        emit BalanceUpdated(_did, balances[didHash]); // 触发余额更新事件
    }

    function withdraw(string memory _did, uint256 amount) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        require(bytes(didDocuments[didHash].created).length > 0, "DID does not exist");

        require(balances[didHash] >= amount, "Insufficient balance");

        balances[didHash] -= amount; // 减少余额

        emit BalanceUpdated(_did, balances[didHash]); // 触发余额更新事件
    }
    function getBalance(string memory _did) public view returns (uint256) {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        require(bytes(didDocuments[didHash].created).length > 0, "DID does not exist");

        return balances[didHash]; // 返回余额
    }

    function getRecordsByTimeRange() public view returns (uint256[] memory) {
        uint256[] memory indices = new uint256[](10);
        uint256 currentIndex = 0;
        for (uint256 i = 0; i < 10; i++) {
            indices[currentIndex] = i;
            currentIndex++;
        }
        return indices;
    }

    // 关联资产到已注册的 DID
    function associateAsset(
        string memory _did,
        string memory _assetId,
        uint8 _rightType
    ) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        require(bytes(didDocuments[didHash].created).length != 0, "DID does not exist");

        AssetData memory newAsset = AssetData({
            assetId: _assetId,
            rightType: _rightType
        });

        DIDDocument storage document = didDocuments[didHash];
        document.assets[_assetId] = newAsset;
        document.assetIds.push(_assetId); // 添加到 ID 列表
        assetCounts[didHash] = document.assetIds.length; // 更新对应 DID 的资产数量
        document.updated = uint2str(block.timestamp);

        emit AssetAssociated(_did, _assetId, _rightType);
    }

    // 更新 DID 文档（用户友好版本）
    function updateDIDField(
        string memory _did,
        string memory fieldName,  // 指定需要更新的字段名称
        string memory newValue    // 更新后的新值
    ) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        DIDDocument storage document = didDocuments[didHash];
        require(bytes(document.created).length != 0, "DID does not exist");

        // 根据传入的字段名称更新相应的字段
        if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("userType"))) {
            document.userType = newValue;
        } else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("publicKey1"))) {
            document.publicKey1 = newValue;
        } else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("publicKey2"))) {
            document.publicKey2 = newValue;
        } else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("signatureValue"))) {
            document.signatureValue = newValue;
        } else {
            revert("Invalid field name provided");
        }

        // 更新修改时间
        document.updated = uint2str(block.timestamp);
        emit DIDUpdated(_did, document.updated);
    }
    function existsDID(string memory _did) public view returns (bool) {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        return bytes(didDocuments[didHash].created).length > 0; // 如果存在，返回 true
    }

    // 查询 DID 文档，返回资产信息
    function queryDID(string memory _did)
    public
    view
    returns (
        string memory userType,
        string memory created,
        string memory updated,
        string memory publicKey1,
        string memory publicKey2,
        string memory signatureValue,
        AssetData[] memory assets
    ) {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        DIDDocument storage document = didDocuments[didHash];
        require(bytes(document.created).length > 0, "DID not found");

        // 获取资产数量
        uint assetCount = document.assetIds.length;

        // 初始化 memory 数组
        AssetData[] memory assetArray = new AssetData[](assetCount);
        for (uint i = 0; i < assetCount; i++) {
            assetArray[i] = document.assets[document.assetIds[i]];
        }

        return (
            document.userType,
            document.created,
            document.updated,
            document.publicKey1,
            document.publicKey2,
            document.signatureValue,
            assetArray  // 返回手动复制的 memory 数组
        );
    }

    // 删除资产
    function deleteAsset(string memory _did, string memory _assetId) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        DIDDocument storage document = didDocuments[didHash];
        require(bytes(document.created).length != 0, "DID does not exist");

        // 找到并删除对应的资产
        require(bytes(document.assets[_assetId].assetId).length > 0, "Asset not found");
        delete document.assets[_assetId];

        // 手动移除资产 ID
        uint assetIndex;
        for (uint i = 0; i < document.assetIds.length; i++) {
            if (keccak256(abi.encodePacked(document.assetIds[i])) == keccak256(abi.encodePacked(_assetId))) {
                assetIndex = i;
                break;
            }
        }

        document.assetIds[assetIndex] = document.assetIds[document.assetIds.length - 1]; // 替换为最后一个元素
        document.assetIds.pop(); // 手动缩减数组长度
        assetCounts[didHash] = document.assetIds.length; // 更新对应 DID 的资产数量

        // 更新更新时间
        document.updated = uint2str(block.timestamp);
        emit AssetDeleted(_did, _assetId);
    }
    function updateAssetRightType(
        string memory _did,
        string memory _assetId,
        uint8 _newRightType
    ) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        DIDDocument storage document = didDocuments[didHash];
        require(bytes(document.created).length != 0, "DID does not exist");

        // 查找对应资产
        AssetData storage asset = document.assets[_assetId];
        require(bytes(asset.assetId).length > 0, "Asset not found");

        // 更新权利类型
        asset.rightType = _newRightType;

        // 更新修改时间
        document.updated = uint2str(block.timestamp);

        emit AssetRightTypeUpdated(_did, _assetId, _newRightType); // 触发事件
    }
    function getAssetCount(string memory _did) public view returns (uint256) {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        require(bytes(didDocuments[didHash].created).length > 0, "DID does not exist");

        return assetCounts[didHash]; // 返回对应 DID 的资产数量
    }



    // 删除 DID 和所有关联的资产
    function deleteDID(string memory _did) public {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        DIDDocument storage document = didDocuments[didHash];
        require(bytes(document.created).length > 0, "DID not found");

        // 逐个删除资产并发出事件
        for (uint i = 0; i < document.assetIds.length; i++) {
            emit AssetDeleted(_did, document.assetIds[i]);
            delete document.assets[document.assetIds[i]];
        }

        // 将文档内容重置为空
        document.userType = "";
        document.created = "";
        document.updated = "";
        document.publicKey1 = "";
        document.publicKey2 = "";
        document.signatureValue = "";
        delete document.assetIds; // 删除所有资产 ID

        // 删除 dids 中的指定 DID
        for (uint j = 0; j < dids.length; j++) {
            if (keccak256(abi.encodePacked(dids[j])) == keccak256(abi.encodePacked(_did))) {
                dids[j] = dids[dids.length - 1]; // 替换为最后一个元素
                dids.pop(); // 手动减少数组长度
                break;
            }
        }

        emit DIDDeleted(_did);
    }

    // 从链上提取 DID 文档签名和公钥
    function extractDIDSignatureAndKeys(string memory _did)
    public
    view
    returns (
        string memory signatureValue,
        string memory publicKey1,
        string memory publicKey2
    ) {
        bytes32 didHash = keccak256(abi.encodePacked(_did));
        DIDDocument storage document = didDocuments[didHash];
        require(bytes(document.created).length > 0, "DID not found");

        return (document.signatureValue, document.publicKey1, document.publicKey2);
    }

    // 获取 DID 总数
    function getDIDCount() public view returns (uint) {
        return dids.length;
    }

    // 获取所有的 DID
    function getAllDIDs() public view returns (string[] memory) {
        return dids;
    }

    // 辅助函数：将 uint 转换为 string
    function uint2str(uint _i) internal pure returns (string memory) {
        emit TestDIDDeleted(9);
        if (_i == 0) {
            return "0";
        }
        emit TestDIDDeleted(10);
        uint j = _i;
        uint len;
        while (j != 0) {
            len++;
            j /= 10;
        }
        emit TestDIDDeleted(11);
        bytes memory bstr = new bytes(len);
        uint k = len - 1;
        emit TestDIDDeleted(12);
        while (_i != 0) {
            bstr[k--] = bytes1(uint8(48 + _i % 10));
            _i /= 10;
        }
        emit TestDIDDeleted(13);
        return string(bstr);
    }
}
