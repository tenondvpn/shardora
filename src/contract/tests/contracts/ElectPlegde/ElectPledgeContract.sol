// SPDX-License-Identifier: MIT
pragma solidity >=0.6.0 <0.9.0;

contract ElectPledgeContract {
    uint64 nowElectHeight;
    uint64 public constant FROZEN_N = 20;    
    mapping(address => mapping(uint64 => uint64)) public accountEhStakeMap; // 每个账户为 第 eh 轮选举质押的 stake
    // Auxiliary list to keep track of addresses and election rounds
    mapping(address => uint64[]) public accountEhList;
    address[] public addresses;

    // // todo delet me
    // function setElectHeight(uint64 n) public {
    //     nowElectHeight = n;
    // }


    function getNowElectHeight() public view returns (uint64) {
        return nowElectHeight;
        // return block.electblock_height;
    }


    // todo delet me
    // function setFROZEN_N(uint64 n) public {
    //     FROZEN_N = n;
    // }
    function bytesConcat(
        bytes[] memory arr,
        uint count
    ) public pure returns (bytes memory) {
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
    function u256ToBytes(uint256 x) public pure returns (bytes memory b) {
        b = new bytes(32);
        assembly {
            mstore(add(b, 32), x)
        }
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
    function getAccountStakes(
        address account,
        uint64 eh
    ) public view returns (bytes memory) {
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = ToHex(
            u256ToBytes(accountEhStakeMap[account][eh])
        );
        return bytesConcat(all_bytes, filedCount);
    }

    function getAccountStakesElect(
        address user
    ) public view returns (bytes memory) {
        uint64 electHeight = getNowElectHeight();
        uint64 totalStake = 0;
        uint64[] storage ehList = accountEhList[user];
        uint64 minValidEh = electHeight >= FROZEN_N ? electHeight - FROZEN_N : 0;
        for (uint i = 0; i < ehList.length; i++) {
            uint64 eh = ehList[i];
            if (eh >= minValidEh && eh <= electHeight) {
                totalStake += accountEhStakeMap[user][eh];
            }
        }
        
        bytes[] memory all_bytes = new bytes[](100);
        uint filedCount = 0;
        all_bytes[filedCount++] = ToHex(
            u256ToBytes(totalStake)
        );
        return bytesConcat(all_bytes, filedCount);
    }


    function pledge() public payable {
        uint64 eh = getNowElectHeight();

        if (accountEhStakeMap[msg.sender][eh] == 0) {
            if (accountEhList[msg.sender].length == 0) {
                addresses.push(msg.sender);
            }
            accountEhList[msg.sender].push(eh);
        }
        accountEhStakeMap[msg.sender][eh] += uint64(msg.value);
    }

    // 用户质押的币要冻结 20 轮。 在 20 轮选举后可以体现。
    // 在选举时 root 网络可以汇总 前 20 轮质押的所有币。这样用户就不用为每一轮选举都质押了，可以每 20 轮质押一次。
    function withDraw() public payable {
          uint64 electHeight = getNowElectHeight();
        if (electHeight <= FROZEN_N) {
            return;
        }

        uint64 totalWithdrawn = 0;
        uint64[] storage ehList = accountEhList[msg.sender];

        for (uint i = 0; i < ehList.length; ) {
            uint64 eh = ehList[i];
            if (eh < electHeight - FROZEN_N) {
                totalWithdrawn += accountEhStakeMap[msg.sender][eh];
                delete accountEhStakeMap[msg.sender][eh];
                ehList[i] = ehList[ehList.length - 1];
                ehList.pop();
            } else {
                i++;
            }
        }

        if (ehList.length == 0) {
            // Remove address from the addresses list
            for (uint i = 0; i < addresses.length; i++) {
                if (addresses[i] == msg.sender) {
                    addresses[i] = addresses[addresses.length - 1];
                    addresses.pop();
                    break;
                }
            }
        }

        payable(msg.sender).transfer(totalWithdrawn);
    }
    receive() external payable {
        pledge();
    }

     function getTotalStake(address user) public view returns (uint64) {
        uint64 totalStake = 0;
        uint64[] storage ehList = accountEhList[user];

        for (uint i = 0; i < ehList.length; i++) {
            uint64 eh = ehList[i];
            totalStake += accountEhStakeMap[user][eh];
        }
        return totalStake;
    }

        // Function to get all addresses
    function getAllAddresses() public view returns (address[] memory) {
        return addresses;
    }

    // Function to get all eh values for a given address
    function getAllEh(address user) public view returns (uint64[] memory) {
        return accountEhList[user];
    }
}
