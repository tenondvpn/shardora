// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

// 1. The seller can sell at most coins equal to the pledged quantity
// 2. The pledged currency can only be recovered by the seller
// 3. The manager can forcefully cancel the transaction and return the pledged coins to the seller.
// 4. If the transaction is reported and the seller cannot redeem it, it will be locked,
//    and the manager can release it according to the situation

contract ProxyReencryption {
    bytes32 test_ripdmd_;
    bytes32 enc_init_param_;

    # SetUp：初始化算法，需要用到pbc库
    constructor(bytes memory enc_init_param) {
        enc_init_param_ = ripemd160(enc_init_param);
    }

    function call_proxy_reenc(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    # CreatPath(i)：由用户i选择多个被委托者。按选择顺序生成一个路径（列表），其中存放被委托者的公钥。
    function CreatPath(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    # RKGen：重加密密钥生成，需要用到pbc库
    function PKGen(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    # Upd：token更新算法，需要用到pbc库
    function Upd(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    # Enc：加密，需要用到pbc库
    function Enc(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    # ReEnc：重加密，需要用到pbc库 (这一步包含一个分布式随机数生成协议，即多个代理协商出一个统一的随机数)
    function ReEnc(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
    }

    # Dec：解密，需要用到pbc库
    function Dec(bytes memory params) public {
        test_ripdmd_ = ripemd160(params);
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

    function GetOrdersJson() public view returns(bytes memory) {
        uint validLen = 0;
        bytes[] memory all_bytes = new bytes[](validLen + 2);
        all_bytes[0] = '[';
        uint arrayLength = 0;
        for (uint i=0; i<arrayLength; i++) {
            ++validLen;
        }

        all_bytes[validLen] = ']';
        return bytesConcat(all_bytes, validLen + 1);
    }
}
