1. 合约创建与调用.
    1.1 启动区块链
    1.2 修改 js 脚本端口号
       `src/contract/tests/contracts/ElectPlegde/test.js` 第 21 行 的 nodes ,和 shard_prikey 中修改端口号和节点私钥.
    1.3 运行 js 脚本
        脚本最底部有暴露的 创建合约,质押币,查询质押币,查询当前的选举高度,提取质押币 等接口.
    
2. 合约的修改
    1.1 编写 sol 文件.
    1.2 编译 sol 文件.
        ``` shell
            cd src/contract/tests/contracts/ElectPlegde
            solc --abi ./contracts/ElectPlegde.sol -o ./build
        ```
    注意: 合约的第一句     uint64 nowElectHeight;
    这个变量是底层 hack 进去的.
    通过 `src/protos/prefix_db.h::AddNowElectHeight2Plege` 赋值.
    通过 `ZjchainHost::get_storage` 调用.
    因此如果不清楚具体的 hack 逻辑请勿修改合约中 uint64 nowElectHeight 的声明位置和变量类型.
    在合约中声明其他全局变量时也应该,check 一下该值的取设值逻辑是否正常.
