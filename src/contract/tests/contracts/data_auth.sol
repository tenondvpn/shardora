// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

contract DataAuthorization {
    struct AuthorizationInfo {
        bytes authInfo;
        address author;
        uint256 authId;
    }

    uint256 authId = 0;
    address public owner;
    mapping(uint256 => AuthorizationInfo) public authorizations;
    mapping(address => bool) public valid_managers;

    // 为合约创建一个身份，并且设置初始管理员，管理员拥有数据确权权限
    constructor(address[] memory managers) payable {
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }

        authId = 0;
        valid_managers[msg.sender] = true;
        owner = msg.sender;
    }

    // 新增数据管理员
    function AddManager(address[] memory managers) public {
        require(owner == msg.sender);
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }
    }

    // 删除数据管理员
    function RemoveManager(address[] memory managers) public {
        require(owner == msg.sender);
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            if (valid_managers[managers[i]]) {
                delete valid_managers[managers[i]];
            }
        }
    }

    // 数据确权
    function Authorization(bytes memory authInfo) public payable {
        // 必须是数据管理员
        require(valid_managers[msg.sender]);
        authorizations[authId] = AuthorizationInfo({
            authInfo: authInfo,
            author: msg.sender,
            authId: authId
        });

        authId++;
    }

    // 获取确权详情
    function GetAuthJson() public view returns(bytes memory) {
        bytes[] memory all_bytes = new bytes[](authId + 2);
        all_bytes[0] = '[';
        uint arrayLength = authId;
        uint validLen = 1;
        for (uint i=0; i<arrayLength; i++) {
            all_bytes[i + 1] = GetItemJson(authorizations[i], (i == arrayLength - 1));
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
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

    function GetItemJson(AuthorizationInfo memory item, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"r":';
        all_bytes[filedCount++] = item.authInfo;
        all_bytes[filedCount++] = ',"a":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.author));
        all_bytes[filedCount++] = '","o":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.authId));
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }
}
