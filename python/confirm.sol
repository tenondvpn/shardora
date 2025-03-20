pragma solidity ^0.8.0;

contract Confirm {
    struct ConfirmInfo {
        uint256 timestamp;  //时间戳
        bytes32 root_hash;  //权利树根hash
    }

    address public owner;
    bytes32 local_id;  // 链下映射id
    bytes32 data_id;  // 数据id
    ConfirmInfo[] public confirm_list;
    address[] public owner_list;

    constructor(bytes32 did, bytes32 lid) {
        owner = msg.sender;
        data_id = did;
        local_id = lid;
    }

    // 修饰器：限制只有管理员可以调用
    modifier onlyOwner() {
        require(msg.sender == owner, "Not an owner");
        _;
    }

    function ConfirmData(
        uint256 timestamp,
        bytes32 root_hash
    ) public onlyOwner {
        confirm_list.push(ConfirmInfo(timestamp, root_hash));
    }

    function ChangeOwner(address new_owner) public onlyOwner {
        require(msg.sender != new_owner, "same owner");
        owner_list.push(msg.sender);
        owner = new_owner;
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

    function NumberToHex(bytes memory buffer) public pure returns (bytes memory) {
        bytes memory converted = new bytes(buffer.length * 2);
        bytes memory _base = "0123456789abcdef";
        bool find_first = false;
        uint256 start_idx = 0;
        for (uint256 i = 0; i < buffer.length; i++) {
            converted[start_idx * 2] = _base[uint8(buffer[i]) / _base.length];
            converted[start_idx * 2 + 1] = _base[uint8(buffer[i]) % _base.length];
            if (find_first) {
                start_idx++;
                continue;
            }

            if (converted[start_idx * 2] != '0' || converted[start_idx * 2 + 1] != '0') {
                find_first = true;
                start_idx++;
            }
        }

        if (start_idx == 0) {
            bytes memory new_bytes = new bytes(2);
            new_bytes[0] = '0';
            new_bytes[1] = '0';
            return new_bytes;
        }


        bytes memory new_bytes = new bytes(start_idx * 2);
        for (uint256 i = 0; i < start_idx * 2; ++i) {
            new_bytes[i] = converted[i];
        }

        return new_bytes;
    }

    function toBytes(address a) public pure returns (bytes memory) {
        return abi.encodePacked(a);
    }

    function u256ToBytes(uint256 x) public pure returns (bytes memory b) {
        b = new bytes(32);
        assembly { mstore(add(b, 32), x) }
    }

    function Bytes32toBytes(bytes32 _data) public pure returns (bytes memory) {
        return abi.encodePacked(_data);
    }

    function GetItemJson(ConfirmInfo memory item, bool last) public pure returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = '{"timestamp":"';
        all_bytes[filedCount++] = NumberToHex(u256ToBytes(item.timestamp));
        all_bytes[filedCount++] = '","root_hash":"';
        all_bytes[filedCount++] = ToHex(Bytes32toBytes(item.root_hash));
        if (last) {
            all_bytes[filedCount++] = '}';
        } else {
            all_bytes[filedCount++] = '},';
        }
        return bytesConcat(all_bytes, filedCount);
    }

    function GetAllItemJson(uint256 start_pos, uint256 len) public view returns(bytes memory) {
        bytes[] memory all_bytes = new bytes[](confirm_list.length + 20);
        uint validLen = 0;
        all_bytes[validLen++] = '{"owner":"';
        all_bytes[validLen++] = ToHex(toBytes(owner));
        all_bytes[validLen++] = '","local_id":"';
        all_bytes[validLen++] = ToHex(Bytes32toBytes(local_id));
        all_bytes[validLen++] = '","data_id":"';
        all_bytes[validLen++] = ToHex(Bytes32toBytes(data_id));
        all_bytes[validLen++] = '","confirm_list":"';
        all_bytes[0] = '[';
        uint arrayLength = confirm_list.length;
        uint start_idx = 0;
        uint got_len = 1;
        for (uint i=start_pos; i<arrayLength && got_len <= len; i++) {
            all_bytes[start_idx + 1] = GetItemJson(confirm_list[i], (i == arrayLength - 1 || got_len == len));
            ++validLen;
            ++start_idx;
            ++got_len;
        }

        all_bytes[validLen++] = ']}';
        return bytesConcat(all_bytes, validLen);
    }
}
