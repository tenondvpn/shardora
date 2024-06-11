// SPDX-License-Identifier: MIT
pragma solidity >=0.6.0 <0.9.0;

contract DataContract {
    address public platformAccount;         //平台账户
    address public sellerAddress;           //数据提供者账户
    address public originatorAddress;       //原创者账户
    address public buyerConfirmerAddress;   //买家确权者账号(确权不通过)
    uint256 public minStake;                //卖家制定的最小质押积分
    uint256 public batchRequiredStake;      //每批次交易所需积分
    uint256 public claimFrequency;          //确权频率，和当前批次号共同决定哪些批次分配收益（当前批次以及前面claimFrequency-1个批次）
    bytes32 public rootValue;               //root值
    uint256 public sellerReward;            //数据提供者收益积分(可直接提取)
    uint256 public originatorReward;        //原创者收益积分(可直接提取)
    uint256 public buyerConfirmerReward;    //买家确权者收益积分(可直接提取)
    uint  maxSizeThreshold;                 //买家列表允许的最大长度（防止块数据过大）， todo

    enum OrderStatus {//买家订单状态
        InUse,                   //使用中
        LegalityCheckFailed,     //合法性检测失败
        BuyerSignatureFailed,    //买家的签名验签失败
        SellerSignatureFailed,   //卖家的签名验签失败
        InsufficientStake,       //买家质押币不足
        SellerDelisted,          //卖家主动下架 (终态)
        SellerRightsFailed,      //确权失败下架 (终态)
        BuyerUnsubscribed,       //买家取消    (终态)
        Other
    }

    struct Order {
        uint256 stakeAmount;  //买家剩余质押积分
        OrderStatus status;   //买家订单状态
        uint256[] batchTradingIndices;              //批次id数组，即batchTradings的key
    }

    mapping(address => Order) public buyerOrders;   //买家地址和买家订单映射
    address[] public buyerAddresses;                //所有的买家地址，即buyerOrders的key
    mapping(address => mapping(uint256 => uint256)) public batchTradings;  //key为买家地址，value为当前批次的超时时间

    constructor(
        uint256 _minStake,
        uint256 _batchRequiredStake,
        uint256 _claimFrequency,
        address _sellerAddress
    ) {
        require(_batchRequiredStake % 2 == 0, "_batchRequiredStake should be even");
        platformAccount = msg.sender;
        minStake = _minStake;
        batchRequiredStake = _batchRequiredStake;
        claimFrequency = _claimFrequency;
        sellerAddress = _sellerAddress;
        sellerReward = 0;         //初始为0
        originatorReward = 0;     //初始为0
        buyerConfirmerReward = 0; //初始为0
        maxSizeThreshold = 10000; //默认最大同时支持1w个买家在线交易
    }

    modifier onlyPlatform() {
        require(msg.sender == platformAccount, "Only platform can call this.");
        _;
    }

    //增加新的订阅者，同时质押积分
    function subscribeData() public payable returns (bool success) {
        require(msg.value >= minStake, "The stake amount is less than the minimum required stake.");
        require(msg.value %2 == 0, "The stake amount need even number.");
        require(buyerAddresses.length < maxSizeThreshold, "The buyer count should less than the maxSizeThreshold.");
        require(buyerOrders[msg.sender].stakeAmount == 0, "The buyer has exist.");
        Order memory newOrder;
        newOrder.stakeAmount = msg.value;
        newOrder.status = OrderStatus.InUse;
        buyerOrders[msg.sender] = newOrder;
        buyerAddresses.push(msg.sender);  // 添加新的买家地址到数组中
        return true;
    }

    //修改质押积分，即（买家）积分充值
    function rechargeStake() public payable returns (bool success) {
        require(msg.value %2 == 0, "The sent value need even number.");
        require(buyerOrders[msg.sender].stakeAmount > 0, "The buyer not exist.");
        buyerOrders[msg.sender].stakeAmount += msg.value;
        return true;
    }

     //验签操作
     function verify(bytes memory hash, bytes memory signature, address account) public pure returns (bool) {
        bytes32 ethSignedHash = keccak256(abi.encodePacked("\x19Ethereum Signed Message:\n32", hash));
        (bytes32 r, bytes32 s, uint8 v) = splitSignature(signature);
        return account == ecrecover(ethSignedHash, v, r, s);
    }

    function splitSignature(bytes memory sig) internal pure returns (bytes32 r, bytes32 s, uint8 v) {
        require(sig.length == 65, "Invalid signature length");

        assembly {
            // first 32 bytes, after the length prefix
            r := mload(add(sig, 32))
            // second 32 bytes
            s := mload(add(sig, 64))
            // final byte (first byte of the next 32 bytes)
            v := byte(0, mload(add(sig, 96)))
        }

        // Correctly set the v parameter in case it's 0/1 instead of 27/28
        if(v < 27) v += 27;
    }

    //签名验签接口，需同时传买家和卖家签名，合约仅保存验签通过的待分配收益的批次
    function verifySignatures(
        address buyerAddress,         //买家地址
        address seller,        //卖家地址
        bytes memory buyerSignature,  //买家签名
        bytes memory sellerSignature, //卖家签名
        uint256 batchNumber,          //批次号
        bytes memory hashValue        //当前批次hash值
    ) public onlyPlatform returns (bool success) {
        require(buyerOrders[buyerAddress].stakeAmount>0, "The buyer not exist.");
        require(buyerOrders[buyerAddress].status == OrderStatus.InUse, "The order is not in use.");
        require(buyerOrders[buyerAddress].stakeAmount > batchRequiredStake, "The remaining stake is not enough for a batch.");
        if (buyerOrders[buyerAddress].stakeAmount <= batchRequiredStake) {
            buyerOrders[buyerAddress].status = OrderStatus.InsufficientStake;
            return false;
        } else if (!verify(hashValue, sellerSignature, seller)) {
            buyerOrders[buyerAddress].status = OrderStatus.SellerSignatureFailed;
            return false;
        } else if (!verify(hashValue, buyerSignature, buyerAddress)) {
            buyerOrders[buyerAddress].status = OrderStatus.BuyerSignatureFailed;
            return false;
        } else {
            uint256 extractEarliestTime = block.timestamp + 2 days;
            buyerOrders[buyerAddress].batchTradingIndices.push(batchNumber);  // 添加新的idx到数组中
            batchTradings[buyerAddress][batchNumber] = extractEarliestTime;
            buyerOrders[buyerAddress].stakeAmount = buyerOrders[buyerAddress].stakeAmount - batchRequiredStake;
            return true;
        }
    }

    //买家取消接口，同时赎回质押积分
    function cancelOrder(address buyerAddress) public onlyPlatform returns (bool success) {
        require(buyerOrders[buyerAddress].stakeAmount>0, "The buyer not exist.");
        // 检查订单是否非终态
        require(
            buyerOrders[buyerAddress].status != OrderStatus.SellerDelisted &&
            buyerOrders[buyerAddress].status != OrderStatus.SellerRightsFailed &&
            buyerOrders[buyerAddress].status != OrderStatus.BuyerUnsubscribed,
            "Order is in a final state and cannot be cancelled."
        );

        uint256 refundAmount = buyerOrders[buyerAddress].stakeAmount;
        buyerOrders[buyerAddress].stakeAmount = 0;
        buyerOrders[buyerAddress].status = OrderStatus.BuyerUnsubscribed;

        // 若没有待分配收益的批次，则删除买家订单
        if (buyerOrders[buyerAddress].batchTradingIndices.length == 0) {
           deleteBuyerOrder(buyerAddress);
        }

        payable(buyerAddress).transfer(refundAmount);
        return true;
    }


    //删除map和数组中元素（买家批次数据）
    function deleteTradeIndex(address buyerAddress, uint256 batchNumber) internal {
        require(buyerOrders[buyerAddress].stakeAmount>0, "The buyer not exist.");
        uint256  batchTradingIndicesLength = buyerOrders[buyerAddress].batchTradingIndices.length;
        if (batchTradingIndicesLength > 0) {
            delete batchTradings[buyerAddress][batchNumber];
            for (uint256 i = 0; i < batchTradingIndicesLength; i++) {
                if (buyerOrders[buyerAddress].batchTradingIndices[i] == batchNumber) {
                    // 交换要删除的元素和数组的最后一个元素
                    buyerOrders[buyerAddress].batchTradingIndices[i] = buyerOrders[buyerAddress].batchTradingIndices[batchTradingIndicesLength - 1];
                    // 删除数组的最后一个元素
                    buyerOrders[buyerAddress].batchTradingIndices.pop();
                    break;
                }
            }
        }
    }

      //删除map和数组中元素（买家订单数据）
     function deleteBuyerOrder(address buyerAddress) internal {
        require(buyerOrders[buyerAddress].stakeAmount>0, "The buyer not exist.");
        delete buyerOrders[buyerAddress];
        for (uint256 i = 0; i < buyerAddresses.length; i++) {
            if (buyerAddresses[i] == buyerAddress) {
                // 交换要删除的元素和数组的最后一个元素
                buyerAddresses[i] = buyerAddresses[buyerAddresses.length - 1];
                // 删除数组的最后一个元素
                buyerAddresses.pop();
                break;
            }
        }
     }

   //确权接口，即分配收益
      function distributeRewards(
        bool verifiedSuccess,          //true：确权通过，false:确权不通过
        uint256 batchNumber,           //批次号
        address originator,     //原创者地址
        address confirmerAddress  //买家确权者地址,即买家
    ) public onlyPlatform returns (bool success) {
        require(originator != address(0), "Invalid originator address.");
        require(confirmerAddress != address(0), "Invalid buyer confirmer address.");
        require(buyerOrders[confirmerAddress].stakeAmount>0, "The buyer not exist.");
        for (uint256 i = 0; i < claimFrequency; i++) {
            uint256 currentBatchNumber = batchNumber - i;
            if (verifiedSuccess) { //确权通过，收益归数据提供者
                sellerReward += batchRequiredStake;
            } else {//确权不通过，收益原创者和买家确权者各一半
                originatorReward += batchRequiredStake / 2;
                buyerConfirmerReward += batchRequiredStake / 2;
                changeOriginatorAddress(originator);
                changeBuyerConfirmerAddress(confirmerAddress) ;
            }
            deleteTradeIndex(confirmerAddress, currentBatchNumber);
        }
        return true;
    }

    //更新root值，仅允许平台账号修改
    function updateRoot(bytes32 _rootValue) public onlyPlatform returns (bool success) {
        rootValue = _rootValue;
        return true;
    }

// 卖家主动下架接口
    function removeListing() public onlyPlatform returns (bool success) {
        // 遍历所有买家订单
        for (uint256 i = 0; i < buyerAddresses.length; i++) {
            address buyerAddress = buyerAddresses[i];
            // 检查订单状态是否非终态
            if (
                buyerOrders[buyerAddress].status == OrderStatus.InUse ||
                buyerOrders[buyerAddress].status == OrderStatus.LegalityCheckFailed ||
                buyerOrders[buyerAddress].status == OrderStatus.BuyerSignatureFailed ||
                buyerOrders[buyerAddress].status == OrderStatus.SellerSignatureFailed ||
                buyerOrders[buyerAddress].status == OrderStatus.InsufficientStake
            ) {
                // 修改订单状态为主动下架
                buyerOrders[buyerAddress].status = OrderStatus.SellerDelisted;

                // 为买家赎回剩余质押积分
                uint256 refundAmount = buyerOrders[buyerAddress].stakeAmount;
                buyerOrders[buyerAddress].stakeAmount = 0;
                payable(buyerAddress).transfer(refundAmount);
            }
        }

        return true;
    }

    // 盗用者下架接口
    function delistPlagiarist() public onlyPlatform returns (bool success) {
        // 已调用过确权不通过接口，即已更新原创者账号地址和买家确权者账户地址
        require(originatorAddress != address(0) && buyerConfirmerAddress != address(0), "Rights confirmation has not been completed.");

        // 修改非终态的买家订单的订单状态为确权不通过下架
        for (uint256 i = 0; i < buyerAddresses.length; i++) {
            address buyerAddress = buyerAddresses[i];
            if (buyerOrders[buyerAddress].status != OrderStatus.SellerDelisted &&
                buyerOrders[buyerAddress].status != OrderStatus.SellerRightsFailed &&
                buyerOrders[buyerAddress].status != OrderStatus.BuyerUnsubscribed) {
                buyerOrders[buyerAddress].status = OrderStatus.SellerRightsFailed;
            }
        }

        // 为所有买家提取剩余质押积分
       for (uint256 i = 0; i < buyerAddresses.length; i++) {
            address buyerAddress = buyerAddresses[i];
            uint256 remainingStake = buyerOrders[buyerAddress].stakeAmount;
            buyerOrders[buyerAddress].stakeAmount = 0;
            payable(buyerAddress).transfer(remainingStake);
        }

        // 为原创者和买家确权者分别提取积分
        if (originatorReward > 0) {
            originatorReward = 0;
            payable(originatorAddress).transfer(originatorReward);
        }

        if (buyerConfirmerReward > 0) {
            buyerConfirmerReward = 0;
            payable(buyerConfirmerAddress).transfer(buyerConfirmerReward);
        }

        return true;
    }

   //修改平台账号管理者，仅允许当前平台账号修改
    function changePlatformAccount(address newPlatformAccount) public {
        require(msg.sender == platformAccount, "Only existing platform account can change the platform account.");
        platformAccount = newPlatformAccount;
    }

     //（确权不通过时）修改原创者，仅允许平台账号修改
    function changeOriginatorAddress(address newOriginatorAddress) public onlyPlatform {
        if( originatorAddress  == address(0)){
              originatorAddress = newOriginatorAddress;
        }
    }

    //（确权不通过时）修改买家确权者，仅允许平台账号修改
    function changeBuyerConfirmerAddress(address newBuyerConfirmerAddress) public onlyPlatform {
        if( originatorAddress  == address(0)){
               buyerConfirmerAddress = newBuyerConfirmerAddress;
        }
    }

    //数据提供者提取收益积分
    function withdrawsellerReward() public  returns (bool success){
        require(msg.sender == sellerAddress || msg.sender == platformAccount, "Only the seller can withdraw the owner reward.");
        sellerReward = 0;        
        payable(sellerAddress).transfer(sellerReward);
        return true;
    }

   // 数据提供者提取收益积分接口（定时任务）
   function extractsellerReward() public onlyPlatform returns (bool success) {
        // 遍历所有买家订单
         for (uint256 i = 0; i < buyerAddresses.length; i++) {
            address buyerAddress = buyerAddresses[i];
            // 遍历所有批量交易
             for (uint256 j = 0; j < buyerOrders[buyerAddress].batchTradingIndices.length; j++) {
                uint256 batchNumber = buyerOrders[buyerAddress].batchTradingIndices[j];
                // 检查超时时间，超时收益归数据提供者
                if (block.timestamp > batchTradings[buyerAddress][batchNumber]) {
                    sellerReward += batchRequiredStake;
                    deleteTradeIndex(buyerAddress, batchNumber);
                }
            }
        }
        
        // 调用withdrawsellerReward接口
        return withdrawsellerReward();
    }

    //数据原创者提取收益积分
    function withdrawOriginatorReward() public  {
        require(msg.sender == originatorAddress || msg.sender == platformAccount, "Only the originator can withdraw the originator reward.");
        originatorReward = 0;
        payable(originatorAddress).transfer(originatorReward);
    }

   //买家确权者提取确权激励积分
    function withdrawBuyerConfirmerReward() public {
        require(msg.sender == buyerConfirmerAddress || msg.sender == platformAccount, "Only the buyer confirmer can withdraw the buyer confirmer reward.");
        buyerConfirmerReward = 0;
        payable(buyerConfirmerAddress).transfer(buyerConfirmerReward);
    }

  // 批次签名查询接口
    function getBatchSignatures(address buyerAddress, uint256 batchNumber) public view returns (uint256) {
        return batchTradings[buyerAddress][batchNumber];
    }

    // Implement the logic for contract destruction.
    // 合约销毁接口
  function destroy() public {
        require(msg.sender == platformAccount, "Only platform account can destroy the contract.");
        // 检查所有买家订单是否已处理完
        for (uint256 i = 0; i < buyerAddresses.length; i++) {
            address buyerAddress = buyerAddresses[i];
            require(
                buyerOrders[buyerAddress].status == OrderStatus.SellerDelisted ||
                buyerOrders[buyerAddress].status == OrderStatus.SellerRightsFailed ||
                buyerOrders[buyerAddress].status == OrderStatus.BuyerUnsubscribed,
                "There are still active orders."
            );
           require(buyerOrders[buyerAddress].batchTradingIndices.length == 0, "There are still unextracted profits."
          );
        }

        // 为数据提供者(即卖家)提取收益
        if (sellerReward > 0) {
            payable(sellerAddress).transfer(sellerReward);
            sellerReward = 0;
        }

        // 为数据原创者提取收益
        if (originatorReward > 0) {
            payable(originatorAddress).transfer(originatorReward);
            originatorReward = 0;
        }

        // 为买家确权者提取收益
        if (buyerConfirmerReward > 0) {
            payable(buyerConfirmerAddress).transfer(buyerConfirmerReward);
            buyerConfirmerReward = 0;
        }

        // 销毁合约，并设置平台账号为数据合约销毁的受益者
        selfdestruct(payable(platformAccount));
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

    function convertBytes32ToBytes(bytes32 data) public pure returns (bytes memory) {
        return abi.encodePacked(data);
    }
 
    function u256ToBytes(uint256 x) public pure returns (bytes memory b) {
        b = new bytes(32);
        assembly { mstore(add(b, 32), x) }
    }    
  
    function GetAuthJson() public view returns (bytes memory) {
         bytes[] memory all_bytes = new bytes[](100);
         uint filedCount = 0;
         all_bytes[filedCount++] = '[{"rootValue":"';
         all_bytes[filedCount++] = ToHex(convertBytes32ToBytes(rootValue));
         all_bytes[filedCount++] = '"}]';
         return bytesConcat(all_bytes, filedCount);
     }

    function GetRemainingStakeAmount(address buyer) public view returns (bytes memory) {
         uint256 stakeAmount = 0; 
         if(buyerOrders[buyer].stakeAmount >0){
           stakeAmount =  buyerOrders[buyer].stakeAmount;
         }
         bytes[] memory all_bytes = new bytes[](100);
         uint filedCount = 0;
         all_bytes[filedCount++] = '[{"amount":"';
         all_bytes[filedCount++] = ToHex(u256ToBytes(stakeAmount));
         all_bytes[filedCount++] = '"}]';
         return bytesConcat(all_bytes, filedCount);
    }
}
