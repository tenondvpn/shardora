// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * @title StringKeyValueDatabase
 * @dev 纯字符串键值对存储合约，支持批量写入、逗号分隔批量获取 (CSV)、前缀分页检索等
 */
contract StringKeyValueDatabase {
    // ----------------------------------------------------
    // 存储结构
    // ----------------------------------------------------
    struct Entry {
        string key;
        string value;
    }

    struct QueryResult {
        string key;
        string value;
        bool exists;
    }

    string[] private keys;
    mapping(bytes32 => string) private store;
    mapping(bytes32 => uint256) private keyToIndex;
    mapping(bytes32 => bool) private existsMap;

    event Set(string key, string value);
    event BatchSetCompleted(uint256 count);
    event Deleted(string key);

    error KeyNotFound(string key);
    error EmptyKey();
    error ArrayLengthMismatch();
    error EmptyBatch();

    // ----------------------------------------------------
    // 写入与批量写入
    // ----------------------------------------------------
    function _set(string calldata _key, string calldata _value) internal {
        if (bytes(_key).length == 0) revert EmptyKey();
        bytes32 kHash = keccak256(bytes(_key));

        if (!existsMap[kHash]) {
            keyToIndex[kHash] = keys.length;
            keys.push(_key);
            existsMap[kHash] = true;
        }

        store[kHash] = _value;
        emit Set(_key, _value);
    }

    function set(string calldata _key, string calldata _value) external {
        _set(_key, _value);
    }

    function batchSet(Entry[] calldata _entries) external {
        uint256 len = _entries.length;
        if (len == 0) revert EmptyBatch();

        for (uint256 i = 0; i < len; ) {
            _set(_entries[i].key, _entries[i].value);
            unchecked { ++i; }
        }

        emit BatchSetCompleted(len);
    }

    function batchSet(string[] calldata _keys, string[] calldata _values) external {
        uint256 len = _keys.length;
        if (len == 0) revert EmptyBatch();
        if (len != _values.length) revert ArrayLengthMismatch();

        for (uint256 i = 0; i < len; ) {
            _set(_keys[i], _values[i]);
            unchecked { ++i; }
        }

        emit BatchSetCompleted(len);
    }

    // ----------------------------------------------------
    // 单条与常规批量读取
    // ----------------------------------------------------
    function get(string calldata _key) external view returns (string memory) {
        bytes32 kHash = keccak256(bytes(_key));
        if (!existsMap[kHash]) revert KeyNotFound(_key);
        return store[kHash];
    }

    function exists(string calldata _key) external view returns (bool) {
        return existsMap[keccak256(bytes(_key))];
    }

    function batchGet(string[] calldata _keys) external view returns (string[] memory) {
        uint256 len = _keys.length;
        string[] memory values = new string[](len);

        for (uint256 i = 0; i < len; ) {
            bytes32 kHash = keccak256(bytes(_keys[i]));
            if (existsMap[kHash]) {
                values[i] = store[kHash];
            }
            unchecked { ++i; }
        }

        return values;
    }

    function batchGetDetailed(string[] calldata _keys) external view returns (QueryResult[] memory) {
        uint256 len = _keys.length;
        QueryResult[] memory results = new QueryResult[](len);

        for (uint256 i = 0; i < len; ) {
            bytes32 kHash = keccak256(bytes(_keys[i]));
            bool isPresent = existsMap[kHash];

            results[i] = QueryResult({
                key: _keys[i],
                value: isPresent ? store[kHash] : "",
                exists: isPresent
            });

            unchecked { ++i; }
        }

        return results;
    }

    // ----------------------------------------------------
    // 新增：逗号分隔批量获取 (CSV Batch Get)
    // ----------------------------------------------------

    /**
     * @notice 支持传入形如 "key1,key2,key3" 的逗号分隔字符串批量获取数据
     * @param _keysCsv 以逗号分割的 Key 集合字符串
     * @return 包含 key、value 和 exists 状态的详细结果数组
     */
    function batchGetByCsv(string calldata _keysCsv) external view returns (QueryResult[] memory) {
        bytes memory raw = bytes(_keysCsv);
        if (raw.length == 0) {
            return new QueryResult[](0);
        }

        // 1. 统计 Key 数量 (逗号数量 + 1)
        uint256 count = 1;
        uint256 len = raw.length;
        for (uint256 i = 0; i < len; ) {
            if (raw[i] == 0x2C) { // 0x2C 为逗号 ',' 的 ASCII 码
                count++;
            }
            unchecked { ++i; }
        }

        QueryResult[] memory results = new QueryResult[](count);
        uint256 startIndex = 0;
        uint256 itemIndex = 0;

        // 2. 切割子串并读取值
        for (uint256 i = 0; i <= len; ) {
            if (i == len || raw[i] == 0x2C) {
                // 提取 [startIndex, i) 区间的字符串作为 key
                string memory keyStr = _substring(raw, startIndex, i);
                bytes32 kHash = keccak256(bytes(keyStr));
                bool isPresent = existsMap[kHash];

                results[itemIndex] = QueryResult({
                    key: keyStr,
                    value: isPresent ? store[kHash] : "",
                    exists: isPresent
                });

                itemIndex++;
                startIndex = i + 1;
            }
            unchecked { ++i; }
        }

        return results;
    }

    /**
     * @notice 简化版：通过逗号分割字符串批量获取，仅返回值列表 string[]
     * @param _keysCsv 以逗号分割的 Key 集合字符串
     */
    function batchGetValuesByCsv(string calldata _keysCsv) external view returns (string[] memory) {
        bytes memory raw = bytes(_keysCsv);
        if (raw.length == 0) {
            return new string[](0);
        }

        uint256 count = 1;
        uint256 len = raw.length;
        for (uint256 i = 0; i < len; ) {
            if (raw[i] == 0x2C) {
                count++;
            }
            unchecked { ++i; }
        }

        string[] memory values = new string[](count);
        uint256 startIndex = 0;
        uint256 itemIndex = 0;

        for (uint256 i = 0; i <= len; ) {
            if (i == len || raw[i] == 0x2C) {
                string memory keyStr = _substring(raw, startIndex, i);
                bytes32 kHash = keccak256(bytes(keyStr));
                if (existsMap[kHash]) {
                    values[itemIndex] = store[kHash];
                }
                itemIndex++;
                startIndex = i + 1;
            }
            unchecked { ++i; }
        }

        return values;
    }

    // 内部函数：截取 bytes 区间转换为 string
    function _substring(
        bytes memory data,
        uint256 start,
        uint256 end
    ) internal pure returns (string memory) {
        if (end <= start) {
            return "";
        }
        uint256 subLen = end - start;
        bytes memory sub = new bytes(subLen);
        for (uint256 i = 0; i < subLen; ) {
            sub[i] = data[start + i];
            unchecked { ++i; }
        }
        return string(sub);
    }

    // ----------------------------------------------------
    // 分页与全量辅助
    // ----------------------------------------------------
    function totalKeys() external view returns (uint256) {
        return keys.length;
    }

    function getKeyAt(uint256 _index) external view returns (string memory) {
        require(_index < keys.length, "Index out of bounds");
        return keys[_index];
    }

    function getPaginated(uint256 _offset, uint256 _limit) external view returns (Entry[] memory) {
        uint256 total = keys.length;
        if (_offset >= total || _limit == 0) {
            return new Entry[](0);
        }

        uint256 end = _offset + _limit;
        if (end > total) {
            end = total;
        }

        uint256 size = end - _offset;
        Entry[] memory result = new Entry[](size);

        for (uint256 i = 0; i < size; ) {
            string memory k = keys[_offset + i];
            bytes32 kHash = keccak256(bytes(k));
            result[i] = Entry({
                key: k,
                value: store[kHash]
            });
            unchecked { ++i; }
        }

        return result;
    }

    // ----------------------------------------------------
    // 前缀遍历与分页功能 (Prefix Scan)
    // ----------------------------------------------------
    function getByPrefix(
        string calldata _prefix,
        uint256 _offset,
        uint256 _limit
    ) external view returns (Entry[] memory) {
        if (_limit == 0) {
            return new Entry[](0);
        }

        bytes memory prefixBytes = bytes(_prefix);
        uint256 total = keys.length;

        uint256 matchCount = 0;
        uint256 resultSize = 0;

        for (uint256 i = 0; i < total; ) {
            if (_startsWith(bytes(keys[i]), prefixBytes)) {
                if (matchCount >= _offset && resultSize < _limit) {
                    resultSize++;
                }
                matchCount++;
                if (resultSize == _limit) {
                    break;
                }
            }
            unchecked { ++i; }
        }

        if (resultSize == 0) {
            return new Entry[](0);
        }

        Entry[] memory result = new Entry[](resultSize);
        uint256 currentMatch = 0;
        uint256 inserted = 0;

        for (uint256 i = 0; i < total && inserted < resultSize; ) {
            string memory currentKey = keys[i];
            if (_startsWith(bytes(currentKey), prefixBytes)) {
                if (currentMatch >= _offset) {
                    bytes32 kHash = keccak256(bytes(currentKey));
                    result[inserted] = Entry({
                        key: currentKey,
                        value: store[kHash]
                    });
                    inserted++;
                }
                currentMatch++;
            }
            unchecked { ++i; }
        }

        return result;
    }

    function _startsWith(bytes memory str, bytes memory prefix) internal pure returns (bool) {
        if (prefix.length > str.length) {
            return false;
        }
        if (prefix.length == 0) {
            return true;
        }

        uint256 pLen = prefix.length;
        for (uint256 i = 0; i < pLen; ) {
            if (str[i] != prefix[i]) {
                return false;
            }
            unchecked { ++i; }
        }
        return true;
    }

    // ----------------------------------------------------
    // 删除
    // ----------------------------------------------------
    function remove(string calldata _key) external {
        bytes32 kHash = keccak256(bytes(_key));
        if (!existsMap[kHash]) revert KeyNotFound(_key);

        uint256 indexToDelete = keyToIndex[kHash];
        uint256 lastIndex = keys.length - 1;

        if (indexToDelete != lastIndex) {
            string memory lastKey = keys[lastIndex];
            bytes32 lastKeyHash = keccak256(bytes(lastKey));

            keys[indexToDelete] = lastKey;
            keyToIndex[lastKeyHash] = indexToDelete;
        }

        keys.pop();

        delete keyToIndex[kHash];
        delete store[kHash];
        delete existsMap[kHash];

        emit Deleted(_key);
    }
}

