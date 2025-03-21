// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

contract DataAuthorization {
    struct AuthorizationInfo {
        bytes authInfo;
        address author;
        uint256 authId;
		uint256 blockNumber;
		uint256 blockTm;
		bytes32 blockHash;
    }

    uint256 authId = 0;
    address public owner;
	bytes metaData; // 业务元数据
    mapping(uint256 => AuthorizationInfo) public authorizations;
    mapping(address => bool) public valid_managers;

    // 为合约创建一个身份，并且设置初始管理员，管理员拥有数据确权权限
    constructor(address[] memory managers, bytes memory meta, bytes memory authInfo) payable {
        uint arrayLength = managers.length;
        for (uint i=0; i<arrayLength; i++) {
            valid_managers[managers[i]] = true;
        }

        authId = 0;
        valid_managers[msg.sender] = true;
        owner = msg.sender;
		metaData = meta;
		// 构造时确权一波
		if (authInfo.length != 0) {
			authorizations[authId] = AuthorizationInfo({
				authInfo: authInfo,
				author: msg.sender,
				authId: authId,
				blockNumber: block.number,
				blockTm: block.timestamp,
				blockHash: blockhash(block.number)
				});

			authId++;		
		}	
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
            authId: authId,
			blockNumber: block.number,
			blockTm: block.timestamp,
			blockHash: blockhash(block.number)
        });

        authId++;
    }

    // 获取确权详情
    function GetAuthJson(uint32 offset, uint32 limit) public view returns(bytes memory) {
        bytes[] memory all_bytes = new bytes[](limit + 8);
        uint l = 0;
        all_bytes[l++] = '{"data":[';
		uint32 arrayLength = uint32(authId);
		uint32 real_limit = limit;
        if (limit + offset > arrayLength) {
            real_limit = arrayLength - offset;
        }
        for (uint i = offset; i < real_limit + offset; i++) {
            all_bytes[l++] = GetItemJson(authorizations[i], (i == real_limit + offset - 1));
        }

		all_bytes[l++] = '],"total":"';
        all_bytes[l++] = ToHex(abi.encodePacked(arrayLength));
        all_bytes[l++] = '","offset":"';
		all_bytes[l++] = ToHex(abi.encodePacked(offset));
        all_bytes[l++] = '","limit":"';
		all_bytes[l++] = ToHex(abi.encodePacked(limit));
        all_bytes[l++] = '"}';

        return bytesConcat(all_bytes, l);
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
    
    function Bytes32ToHex(bytes32 buffer) public pure returns (bytes memory) {
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
        all_bytes[filedCount++] = '{"authInfo":';
        all_bytes[filedCount++] = item.authInfo;
        all_bytes[filedCount++] = ',"author":"';
        all_bytes[filedCount++] = ToHex(toBytes(item.author));
        all_bytes[filedCount++] = '","id":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.authId));
		all_bytes[filedCount++] = '","blockNumber":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.blockNumber));
        all_bytes[filedCount++] = '","blockTm":"';
        all_bytes[filedCount++] = ToHex(u256ToBytes(item.blockTm));
        all_bytes[filedCount++] = '","blockHash":"';
        all_bytes[filedCount++] = Bytes32ToHex(item.blockHash);
        if (last) {
            all_bytes[filedCount++] = '"}';
        } else {
            all_bytes[filedCount++] = '"},';
        }
        return bytesConcat(all_bytes, filedCount);
    }
}
