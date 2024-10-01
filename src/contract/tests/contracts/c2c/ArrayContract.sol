pragma solidity >=0.6.0 <0.9.0;

contract ArrayContract {
    uint[2**20] public aLotOfIntegers;

    // 注意下面的代码并不是一对动态数组，
    // 而是一个数组元素为一对变量的动态数组（也就是数组元素为长度为 2 的定长数组的动态数组）。
    //  T[k] 和 T[] 总是 T 类型的数组, 即使 T 是数组
    // 因此 bool[2][] 是元素 bool[2] 的动态数组。
    // 所有的状态变量的数据位置都是 storage
    bool[2][] public pairsOfFlags;

    // newPairs 存储在 memory 中 (仅当它是公有的合约函数)
    function setAllFlagPairs(bool[2][] memory newPairs) public {

     // 向一个 storage 的数组赋值会对 ``newPairs`` 进行拷贝，并替代整个 ``pairsOfFlags`` 数组
        pairsOfFlags = newPairs;
    }

    struct StructType {
        uint[] contents;
        uint moreInfo;
    }
    StructType public s;

    function f(uint[] memory c) public {
        // 保存引用
        StructType storage g = s;

        // 同样改变了 ``s.moreInfo``.
        g.moreInfo = 2;

        // 进行了拷贝，因为 ``g.contents`` 不是本地变量，而是本地变量的成员
        g.contents = c;
    }

    function setFlagPair(uint index, bool flagA, bool flagB) public {
        // 访问不存在的索引将引发异常
        pairsOfFlags[index][0] = flagA;
        pairsOfFlags[index][1] = flagB;
    }

    function changeFlagArraySize(uint newSize) public {
       // 使用 push 和 pop 是更改数组长度的唯一方法

        if (newSize < pairsOfFlags.length) {
            while (pairsOfFlags.length > newSize)
                pairsOfFlags.pop();
        } else if (newSize > pairsOfFlags.length) {
            while (pairsOfFlags.length < newSize)
                pairsOfFlags.push();
        }
    }

    function clear() public {
        // 这些完全清除了数组
        delete pairsOfFlags;
        delete aLotOfIntegers;
        // 效果相同（和上面）
        //pairsOfFlags.length = new bool[2][](0);   //chengzongxing  这一行有异常
    }

    bytes byteData;

    function byteArrays(bytes memory data) public {
        // 字节数组（bytes）不一样，它们在没有填充的情况下存储。
        // 可以被视为与 uint8 [] 相同
        byteData = data;
        for (uint i = 0; i < 7; i++)
            byteData.push();
        byteData[3] = 0x08;
        delete byteData[2];
    }

    function addFlag(bool[2] memory flag) public returns (uint) {
        pairsOfFlags.push(flag);
        return pairsOfFlags.length;
    }

    function createMemoryArray(uint size) public pure returns (bytes memory) {
        // 使用`new`创建动态内存数组：
        uint[2][] memory arrayOfPairs = new uint[2][](size);

        // 内联（Inline）数组始终是静态大小的，如果只使用字面常量，则必须至少提供一种类型。
        arrayOfPairs[0] = [uint(1), 2];

        // 创建一个动态字节数组：
        bytes memory b = new bytes(200);
        for (uint i = 0; i < b.length; i++)
            b[i] = bytes1(uint8(i));
        return b;
    }
}