// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

// 修改合约名称以反映新的功能
contract HashLoggerAppendKexin {

    // 修改事件定义，用于记录原始参数的哈希和拼接"哈希+kexin"后的完整字节序列
    // originalParameterHash: 用于标识原始输入
    // hashPlusKexinBytes: 将keccak256(原始参数) 的字节 与 "kexin" 字符串的字节拼接后的结果
    // 注意：这里的 bytes hashPlusKexinBytes 移除了 memory 关键字
    event OriginalHashAndAppendedKexin(bytes32 indexed originalParameterHash, bytes hashPlusKexinBytes);

    /**
     * @dev 接收一个参数，计算其keccak256哈希，将"kexin"字符串的字节附加到哈希字节后，
     * 并触发一个事件记录原始参数的哈希和拼接后的完整字节序列。
     * @param _parameter 用户输入的参数（字节数组）。
     */
    // 修改函数名称以反映新的功能
    function logHashAndAppendKexin(bytes memory _parameter) public {
        // 1. 计算原始参数的keccak256哈希
        bytes32 originalHash = keccak256(_parameter);

        // 2. 定义要附加的后缀（"kexin"的字节表示）
        bytes memory suffix = "kexin"; // 在函数内部使用 bytes 类型时，memory 是需要的

        // 3. 拼接数据：这次是 哈希的字节 + 后缀的字节
        // 使用 abi.encodePacked 将 bytes32 (originalHash) 和 bytes (suffix) 拼接
        bytes memory combinedData = abi.encodePacked(originalHash, suffix); // 在函数内部使用 bytes 类型时，memory 是需要的
		
		bool finalOutcome = ripemd160(combinedData);

        // 5. 触发事件打印日志
        emit CombinedDataHashedWithRipemd160(originalKeccak256Hash, finalOutcome);
    }
}