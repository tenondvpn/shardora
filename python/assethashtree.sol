pragma solidity >=0.8.17 <0.9.0;
pragma experimental ABIEncoderV2;

interface mm {
    function existsDID(string  _did) external view returns (bool);
}

contract assethashtree {
    // 定义用户及其权利类型的数据结构
    struct UserRights {
        string userId; // 用户DID
        uint8 rightType; // 权利类型（0: 使用权, 1: 收益权, 2: 监督权, 3: 存储权）
        string roothash;
        string contractaddress;
    }

    // 定义资产数据结构
    struct AssetData {
        string assetId; // 唯一标识符
        string owner; // 资产所有者DID
        string title; // 资产标题
        string assetType; // 资产类型
        string content; // 资产内容
        string created; // 创建时间
        string updated; // 更新时间
        string signatureValue;
        mapping(string => UserRights) userRights; // 使用映射存储用户权利
        string[] userIds; // 存储用户ID以便遍历
    }
    struct Transaction {
        string userId;       // 用户DID
        string assetId;      // 资产ID
        uint8 rightsType;    // 交易权利类型
        string assetType;    // 资产类型
        uint256 amount;      // 交易金额
        bool isActive;       // 是否有效
    }



    mapping(bytes32 => AssetData) private assets; // 使用映射存储资产
    mapping(bytes32 => Transaction) private transactions; // Mapping for transactions
    string[] private assetIds; // 存储所有资产ID
    mm didContract; // DID 合约实例
    bytes32[] private transactionKeys; // 存储所有交易的键


    event AssetRegistered(string assetId, string owner, string created);
    event AssetUpdated(string assetId, string updated);
    event UserAssociated(string assetId, string userId, uint8 rightType);
    event UserDisassociated(string assetId, string userId);
    event AssetDeleted(string assetId, string owner);
    event UserRightsUpdated(string assetId, string userId, uint8 newRightType);
    event TransactionPublished(string assetId, string userId, string assetType, uint8 righttype ,uint256 price);
    event TransactionDeleted(string assetId, string userId, uint8 rightsType);



    constructor(address _didContractAddress) public {
        didContract = mm(_didContractAddress);
    }

    // 注册数据资产
    function registerAsset(
        string memory _assetId,
        string memory _owner,
        string memory _title,
        string memory _assetType,
        string memory _content,
        string memory _signatureValue

    ) public {
        require(didContract.existsDID(_owner), "Owner DID does not exist");

        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        require(bytes(assets[assetHash].assetId).length == 0, "Asset already exists");

        string memory currentTime = uint2str(block.timestamp);
        AssetData storage newAsset = assets[assetHash];
        newAsset.assetId = _assetId;
        newAsset.owner = _owner;
        newAsset.title = _title;
        newAsset.assetType = _assetType;
        newAsset.content = _content;
        newAsset.created = currentTime;
        newAsset.updated = currentTime;
        newAsset.signatureValue = _signatureValue;

        assetIds.push(_assetId); // 将资产ID存入列表中
        emit AssetRegistered(_assetId, _owner, currentTime);
    }
    function publishTransaction(
        string memory _userId,
        string memory _assetId,
        uint8 _rightsType,
        string memory _assetType,
        uint256 _amount
    ) public {
        require(didContract.existsDID(_userId), "User DID does not exist");

        // 创建唯一的交易键
        bytes32 transactionKey = keccak256(abi.encodePacked(_userId, "_", _assetId, "_", _rightsType));

        // 检查交易是否已存在
        require(transactions[transactionKey].isActive == false, "Transaction already exists");

        // 创建新交易
        transactions[transactionKey] = Transaction({
            userId: _userId,
            assetId: _assetId,
            rightsType: _rightsType,
            assetType: _assetType,
            amount: _amount,
            isActive: true
        });
        transactionKeys.push(transactionKey); // 添加到交易键数组

        emit TransactionPublished(_assetId, _userId, _assetType, _rightsType,_amount);
    }
    function deleteTransaction(string memory _userId, string memory _assetId, uint8 _rightsType) public {
        // 创建唯一的交易键
        bytes32 transactionKey = keccak256(abi.encodePacked(_userId, "_", _assetId, "_", _rightsType));

        // 检查交易是否存在
        require(transactions[transactionKey].isActive == true, "Transaction does not exist");

        // 标记交易为非活跃
        transactions[transactionKey].isActive = false; // 或者直接删除，可以根据需要调整

        emit TransactionDeleted(_assetId, _userId, _rightsType);
    }
    function queryTransaction(string memory _userId, string memory _assetId, uint8 _rightsType) public view returns (
        string memory userId,
        string memory assetId,
        uint8 rightsType,
        string memory assetType,
        uint256 amount,
        bool isActive
    ) {
        // 创建唯一的交易键
        bytes32 transactionKey = keccak256(abi.encodePacked(_userId, "_", _assetId, "_", _rightsType));

        // 检查交易是否存在
        require(transactions[transactionKey].isActive == true, "Transaction does not exist");

        // 返回交易信息
        Transaction storage transaction = transactions[transactionKey];
        return (
            transaction.userId,
            transaction.assetId,
            transaction.rightsType,
            transaction.assetType,
            transaction.amount,
            transaction.isActive
        );
    }
    function queryTransactionsByAssetType(string memory _assetType) public view returns (Transaction[] memory) {
        uint count = 0;

        // 先计算符合条件的交易数量
        for (uint i = 0; i < transactionKeys.length; i++) {
            if (transactions[transactionKeys[i]].isActive &&
                keccak256(abi.encodePacked(transactions[transactionKeys[i]].assetType)) == keccak256(abi.encodePacked(_assetType))) {
                count++;
            }
        }

        // 创建一个数组来存储结果
        Transaction[] memory result = new Transaction[](count);
        uint index = 0;

        // 再次遍历并填充结果数组
        for (uint j = 0; j < transactionKeys.length; j++) {
            if (transactions[transactionKeys[j]].isActive &&
                keccak256(abi.encodePacked(transactions[transactionKeys[j]].assetType)) == keccak256(abi.encodePacked(_assetType))) {
                result[index] = transactions[transactionKeys[j]];
                index++;
            }
        }

        return result;
    }

    // 删除资产和与之关联的用户权利
    function deleteAsset(string memory _assetId) public {
        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        AssetData storage asset = assets[assetHash];
        require(bytes(asset.assetId).length > 0, "Asset does not exist");

        // 发出资产删除事件
        emit AssetDeleted(asset.assetId, asset.owner);

        // 清除用户权利
        for (uint i = 0; i < asset.userIds.length; i++) {
            delete asset.userRights[asset.userIds[i]]; // 删除用户权利
        }
        delete asset.userIds; // 清空用户 ID 列表

        // 将资产数据结构中的字段重置为空
        asset.assetId = "";
        asset.owner = "";
        asset.title = "";
        asset.assetType = "";
        asset.content = "";
        asset.created = "";
        asset.updated = "";
        asset.signatureValue = "";

        // 从资产映射中删除资产
        delete assets[assetHash];

        // 从资产 ID 列表中移除资产 ID
        for (uint j = 0; j < assetIds.length; j++) {
            if (keccak256(abi.encodePacked(assetIds[j])) == keccak256(abi.encodePacked(_assetId))) {
                assetIds[j] = assetIds[assetIds.length - 1]; // 替换为最后一个元素
                assetIds.length--; // 移除最后一个元素
                break;
            }
        }
    }
    // 关联用户与资产
    function associateUser(
        string memory _assetId,
        string memory _userId,
        uint8 _rightType,
        string memory _roothash,
        string memory _contractaddress
    ) public {
        require(didContract.existsDID(_userId), "User DID does not exist");

        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        require(bytes(assets[assetHash].assetId).length != 0, "Asset does not exist");

        AssetData storage asset = assets[assetHash];
        asset.userRights[_userId] = UserRights({ userId: _userId, rightType: _rightType,roothash: _roothash,contractaddress:_contractaddress }); // 添加用户权利
        asset.userIds.push(_userId); // 添加用户ID以便遍历

        emit UserAssociated(_assetId, _userId, _rightType);
    }

    // 移除用户与资产的关联
    function disassociateUser(string memory _assetId, string memory _userId) public {
        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        AssetData storage asset = assets[assetHash];
        require(bytes(asset.assetId).length != 0, "Asset does not exist");

        require(bytes(asset.userRights[_userId].userId).length > 0, "User not associated with this asset");

        // 删除用户权利
        delete asset.userRights[_userId];

        // 手动移除用户 ID
        uint indexToRemove;
        bool found = false;

        for (uint i = 0; i < asset.userIds.length; i++) {
            if (keccak256(abi.encodePacked(asset.userIds[i])) == keccak256(abi.encodePacked(_userId))) {
                indexToRemove = i;
                found = true;
                break;
            }
        }

        require(found, "User not associated with this asset");

        // 替换要删除的用户 ID 为最后一个元素并缩减数组
        asset.userIds[indexToRemove] = asset.userIds[asset.userIds.length - 1];
        asset.userIds.length--;

        emit UserDisassociated(_assetId, _userId);
    }
    // 更新用户在资产上的权利类型
    function updateUserRights(
        string memory _assetId,
        string memory _userId,
        uint8 _newRightType
    ) public {
        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        AssetData storage asset = assets[assetHash];
        require(bytes(asset.assetId).length != 0, "Asset does not exist");

        require(bytes(asset.userRights[_userId].userId).length > 0, "User not associated with this asset");

        // 更新用户权利类型
        asset.userRights[_userId].rightType = _newRightType;

        emit UserRightsUpdated(_assetId, _userId, _newRightType);
    }

    // 查询资产信息，包括用户权利
    function queryAsset(string memory _assetId)
    public
    view
    returns (
        string memory title,
        string memory assetType,
        string memory content,
        string memory created,
        string memory updated,
        string memory signatureValue,
        UserRights[] memory userRights
    ) {
        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        AssetData storage asset = assets[assetHash];
        require(bytes(asset.assetId).length > 0, "Asset not found");

        uint userCount = asset.userIds.length;
        UserRights[] memory rightsArray = new UserRights[](userCount);

        for (uint i = 0; i < userCount; i++) {
            rightsArray[i] = asset.userRights[asset.userIds[i]];
        }

        return (
            asset.title,
            asset.assetType,
            asset.content,
            asset.created,
            asset.updated,
            asset.signatureValue,
            rightsArray // 返回用户权利
        );
    }

    function updateAssetField(
        string memory _assetId,
        string memory fieldName,  // Specify which field to update
        string memory newValue    // New value for the specified field
    ) public {
        bytes32 assetHash = keccak256(abi.encodePacked(_assetId));
        AssetData storage asset = assets[assetHash];

        // Ensure the asset exists
        require(bytes(asset.assetId).length != 0, "Asset does not exist");

        // Update the specified field based on fieldName
        if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("title"))) {
            asset.title = newValue;
        } else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("assetType"))) {
            asset.assetType = newValue;
        } else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("content"))) {
            asset.content = newValue;
        } else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("owner"))) {
            asset.owner = newValue;
        }else if (keccak256(abi.encodePacked(fieldName)) == keccak256(abi.encodePacked("signatureValue"))) {
            asset.signatureValue = newValue;
        } else {
            revert("Invalid field name provided"); // Handle invalid field name
        }

        // Update the modified timestamp
        asset.updated = uint2str(block.timestamp);
        emit AssetUpdated(_assetId, asset.updated); // Emit an event for the update
    }

    // 辅助函数：将 uint 转换为 string
    function uint2str(uint _i) internal pure returns (string memory) {
        if (_i == 0) {
            return "0";
        }
        uint j = _i;
        uint len;
        while (j != 0) {
            len++;
            j /= 10;
        }
        bytes memory bstr = new bytes(len);
        uint k = len - 1;
        while (_i != 0) {
            bstr[k--] = byte(uint8(48 + _i % 10));
            _i /= 10;
        }
        return string(bstr);
    }
}
