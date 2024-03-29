// SPDX-License-Identifier: GPL-3.0
pragma solidity ^0.8.0;
pragma experimental ABIEncoderV2;
import "./Heap.sol";
import "./LibArrayForUint256Utils.sol";

contract SellerContract{
    enum State{ BothUncancel, BuyerCanceled, SellerCanceled, BothCancelled, SellerConfirmed, SystemCanceled} //吃单订单状态6种， 双方均未取消，买家取消，卖家取消，双方取消，卖家确认收款, 系统强制取消
    uint  minAmountThreshold;               //限制最小交易数量
    uint  maxSizeThreshold;                 //挂单列表允许的最大长度
    using Heap for Heap.Data;
    Heap.Data priorityQueue;  //优先级由单价和订单号共同决定（单价越高优先级越高，单价相同时，订单号越小优先级越高）
    address payable ownerAddress;
    uint8 loopValue;                //协助计算实际支付金额，卖家根据收款金额对应到订单 
    uint64 orderNo;                 //订单号，自增+2 (卖家挂单合约中订单号为偶数，买家挂单合约中订单号为奇数)
    mapping(uint64=>MakerOrder) public makerOrders;       //挂单列表，挂单订单号至挂单订单映射
    mapping(uint64=>TakerOrder) public takerOrders;       //吃单列表，吃单订单号至吃单订单映射

    constructor(uint _minAmountThreshold, uint _maxSizeThreshold) public {
        priorityQueue.init(); 
        ownerAddress = payable(msg.sender);
        loopValue = 49;
        orderNo  = 0;        //卖家挂单合约初始值为0， 买家挂单合约中初始值为1
        minAmountThreshold = _minAmountThreshold;          
        maxSizeThreshold = _maxSizeThreshold;     
    }
    struct MakerOrder {
        address payable makerAddress;   //ZJB 个人钱包地址
        string nickname;        //昵称
        uint256 creditScore;    //信用分
        uint64 price;           //单价   
        uint256 totalAmount;    //总售量  （随着吃单操作或赎回会改变该值）
        string cardJson;        //支付方式和账号
        uint8 buyerMortgagePercentage; //买家保证金比例，默认100%
        uint8 sellerMortgagePercentage;//卖家保证金比例，默认100%
        uint256[] takerOrderIds;       //吃单订单号列表
    }
    struct TakerOrder {
        address payable makerAddress;   //ZJB 挂单个人钱包  冗余挂单地址(挂单移出后，卖家确认收款或取消订单时需要校验权限)
        address payable takerAddress;   //ZJB 吃单个人钱包
        string nickname;         //昵称
        uint256 amount;          //交易金数量，即购买数量	
        uint256 totalValue;      //实际支付金额（不完全等于单价乘以购买数量）
        State  state;            //吃单状态, 共6种
        uint64 makerOrderId;     //挂单订单号
    }
    //创建一个新的挂单
    function addMakerOrder(uint64 price) payable public {
        address payable makerAddress = payable (msg.sender);
        uint256 totalAmount = msg.value;  //卖家交易金和保证金, 即从个人钱包往合约地址ownerAddress转币
        uint size = priorityQueue.size();
        precondition4MakerOrder(price, totalAmount, size);
        removeMaxOrder(size);    
        uint64 orderId = generateNewOrderId();      
        string memory nickname = "nickname";        //用地址缩写作为昵称
        uint256 creditScore = 100;                  //在数据库中先写死几个值
        uint256[] memory takerOrderIdArray;         
        MakerOrder memory makerOrder = MakerOrder(makerAddress, nickname, creditScore, price, totalAmount, '', 100, 100, takerOrderIdArray);
        makerOrders[orderId] = makerOrder;          
        priorityQueue.insertWithId(price, orderId); 
    }   
    //创建吃单订单
    function addTakerOrder(uint64 makerOrderId) payable public {
        address payable takerAddress = payable (msg.sender);
        uint256 amount = msg.value; //买家保证金数量
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        precondition4TakerOrder(makerOrder, amount);
        uint64 takerOrderId = generateNewOrderId(); 
        string memory nickname = "nickname";       
        uint256 totalValue = calculateTotalValue(makerOrder.price, amount);
        takerOrders[takerOrderId] = TakerOrder(makerOrder.makerAddress, takerAddress, nickname, amount, totalValue, State.BothUncancel, makerOrderId);
        LibArrayForUint256Utils.addValueRepeatedly(makerOrder.takerOrderIds, uint256(takerOrderId));
        removeOrderIfNecessary(makerOrder, makerOrderId); //要触发自动赎回？（挂单总数量不变，但可售数量减少）
    }
    //卖家操作"确认收款"
    function sellerPaymentConfirm (uint64 takerOrderId) public {
        TakerOrder storage takerOrder = takerOrders[takerOrderId];
        uint64 makerOrderId = takerOrder.makerOrderId;
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        precondition4PaymentConfirm(takerOrder);
        takerOrder.state = State.SellerConfirmed; 
        uint256 amount = takerOrder.amount;
        transfer2Account(takerOrder.takerAddress, 2*amount);//往买家地址转交易金和保证金
        if(makerOrder.price == 0){ //情况1：挂单完结，直接往往卖家地址转保证金 
            transfer2Account(takerOrder.makerAddress, amount);
        }else{                          //情况2：挂单未完结，需移除挂单中该吃单订单号，同时修改totalAmount
            LibArrayForUint256Utils.removeByValue(makerOrder.takerOrderIds, uint256(takerOrderId));
            makerOrder.totalAmount = makerOrder.totalAmount - amount; 
        }
        //删除之前，需将吃单订单同步到数据库？
        delete takerOrders[takerOrderId];
        removeOrderIfNecessary(makerOrder, makerOrderId);//有可能会触发自动赎回（挂单总数量减少，可售数量增加）
    }
    //买家或卖家取消订单
    function cancelOrder (uint64 takerOrderId) public {
        TakerOrder storage takerOrder = takerOrders[takerOrderId];
        uint64 makerOrderId = takerOrder.makerOrderId;
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        precondition4CancelOrder(takerOrder);
        State newState = newCancelState(takerOrder);
        takerOrder.state = newState;
        if(newState == State.BothCancelled){
            uint256 amount = takerOrder.amount;
            transfer2Account(takerOrder.takerAddress, amount);//往买家地址转保证金
            if(makerOrder.price == 0){ //情况1：挂单已完结，则直接往卖家地址转交易金和保证金 
                transfer2Account(takerOrder.makerAddress, 2*amount);
            }else{                          //情况2：挂单未完结，需移除挂单中该吃单订单号
                LibArrayForUint256Utils.removeByValue(makerOrder.takerOrderIds, uint256(takerOrderId));
            }
            //删除之前，需将吃单订单同步到数据库？
            delete takerOrders[takerOrderId];
        }
        //removeOrderIfNecessary(makerOrder, makerOrderId); //不触发自动赎回（挂单总数量不变，可售数量增加）
    }
    //赎回指定数量的未锁定币
    function withdrawUnlock (uint64 makerOrderId, uint256 amount) public {
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        uint unLockedAmount = getUnlockedAmount(makerOrder);
        precondition4WithdrawUnlock(makerOrder, amount, unLockedAmount, true);
        transfer2Account(makerOrder.makerAddress, amount);
        makerOrder.totalAmount = makerOrder.totalAmount - amount;
        removeOrderIfNecessary(makerOrder, makerOrderId); //有可能会触发自动赎回（挂单总数量减少，可售数量也减少）
    }
    //赎回所有的未锁定币
    function withdrawUnlockAll(MakerOrder storage makerOrder) internal {
        uint unLockedAmount = getUnlockedAmount(makerOrder);
        if(unLockedAmount >0){
            precondition4WithdrawUnlock(makerOrder, unLockedAmount, unLockedAmount, false);
            transfer2Account(makerOrder.makerAddress, unLockedAmount);
            makerOrder.totalAmount = makerOrder.totalAmount - unLockedAmount;
        }
    }
    //挂单前置条件检查
    function precondition4MakerOrder(uint64 price, uint256 totalAmount, uint size) internal view {
        require(price > 0, "makerOrder:price should greater than zero"); 
        require(totalAmount >= minAmountThreshold, "makerOrder:totalAmount should greater than or equal minAmountThreshold"); //限制最小数量
        require(totalAmount % minAmountThreshold == 0, "makerOrder:totalAmount should be size of minAmountThreshold");  //限制是最小数量的整数倍
        if(size >= maxSizeThreshold){
            Heap.Node memory head = priorityQueue.getMax();
            uint64 maxPrice = head.priority;
            require(price <= maxPrice, "makerOrder:price should less than or equal maxPrice"); //挂单列表已满时，新挂单的单价要小于等于队列中最高单价
        }
    }
    //吃单前置条件检查
    function precondition4TakerOrder(MakerOrder storage makerOrder, uint256 amount) internal view {
        require(makerOrder.price > 0, "takerOrder:makerOrder is not exist");         
        require(makerOrder.makerAddress != msg.sender, "takerOrder:takerOrder and makerOrder should not be the same account"); 
        require(amount >= minAmountThreshold / 2, "takerOrder:amount should greater than or equal minAmountThreshold/2"); 
        require(amount % (minAmountThreshold / 2) == 0, "takerOrder:amount should be size of minAmountThreshold/2"); 
        uint availableAmount = getAvailableAmount(makerOrder);
        require(amount <= availableAmount, "takerOrder:amount is Insufficient");            
    }
    //卖家确认收款前的条件检查
    function precondition4PaymentConfirm(TakerOrder storage takerOrder) internal view {
        require(takerOrder.amount > 0 , "confirm:takerOrder is not exist");      
        require(takerOrder.makerAddress == msg.sender, "confirm:the caller is not the makerAddress"); 
        require(!isCompleted(takerOrder.state), "confirm:takerOrder is completed"); 
    }
    //买家或卖家取消订单前的条件检查
    function precondition4CancelOrder(TakerOrder storage takerOrder) internal view {
        require(takerOrder.amount > 0 , "cancelOrder:takerOrder is not exist");   
        require(takerOrder.makerAddress == msg.sender || takerOrder.takerAddress == msg.sender, "cancelOrder:the caller should be makerAddress or takerAddress");
        State oldState = takerOrder.state; 
        require(State.BothUncancel == oldState || State.BuyerCanceled == oldState || State.SellerCanceled == oldState, "cancelOrder:the state should be BothUncancel or BuyerCanceled or SellerCanceled");//旧的取消状态只能3选1
        if(takerOrder.takerAddress == msg.sender){  //调用者为买家，旧的取消状态只能是双方未取消或卖家已取消
            require(State.BothUncancel == oldState || State.SellerCanceled == oldState, "cancelOrder:the state should be BothUncancel or SellerCanceled");
        }
        if(takerOrder.makerAddress == msg.sender){  //调用者为卖家，旧的取消状态只能是双方未取消或买家已取消
            require(State.BothUncancel == oldState || State.BuyerCanceled == oldState, "cancelOrder:the state should be BothUncancel or BuyerCanceled");
        }
    }
    //计算新的订单取消状态
    function newCancelState(TakerOrder storage takerOrder) internal view returns (State){
        State oldState = takerOrder.state;
        State newState;
        if(oldState == State.BothUncancel && takerOrder.takerAddress == msg.sender){//旧的状态是双方未取消并且调用方是买家 
            newState = State.BuyerCanceled;   
        }
        if(oldState == State.BothUncancel && takerOrder.makerAddress == msg.sender){//旧的状态是双方未取消并且调用方是卖家 
            newState = State.SellerCanceled;   
        }
        if(oldState == State.BuyerCanceled && takerOrder.makerAddress == msg.sender){//旧的状态是买家已取消并且调用方是卖家
            newState = State.BothCancelled;   
        }
        if(oldState == State.SellerCanceled && takerOrder.takerAddress == msg.sender){//旧的状态是卖家已取消并且调用方是买家 
            newState = State.BothCancelled; 
        }
        return newState;
    }
    //赎回未锁定的币前的条件检查（场景1：卖家主动赎回，场景2：卖家主动赎回触发自动赎回，场景3：卖家确认收款触发自动赎回，场景4：买家吃单触发自动赎回，场景5：定时任务检测到挂单超3天触发自动赎回）
    function precondition4WithdrawUnlock(MakerOrder storage makerOrder, uint256 amount, uint256 unLockedAmount, bool isMaker) internal view {
        require(makerOrder.price > 0, "withdraw:makerOrder is not exist");         
        require(!isMaker || (isMaker && makerOrder.makerAddress == msg.sender), "withdraw:the caller is not the makerAddress"); 
        require(amount > 0 && amount <= unLockedAmount, "withdraw:amount should greater than zero and less than or equal unLockedAmount"); //赎回币数量大于0并且赎回币数量小于等于未锁定币数量
    }
    //遍历挂单所有的吃单获取可售数量（根据未锁定数量，计算得到）
    function getAvailableAmount(MakerOrder storage makerOrder) internal view returns(uint){
        return getUnlockedAmount(makerOrder)/2; 
    }
    //遍历挂单的所有未完结的吃单订单获取未锁定币数量
    function getUnlockedAmount(MakerOrder memory makerOrder)internal view returns(uint){
        uint256[] memory takerOrderIds = makerOrder.takerOrderIds;
        uint amountSum = 0;
        for(uint i = 0; i < takerOrderIds.length; i++){
            uint256 takerOrderId = takerOrderIds[i];
            TakerOrder storage takerOrder = takerOrders[uint64(takerOrderId)];
            if(!isCompleted(takerOrder.state)){
                amountSum += takerOrder.amount;
            }
        }
        return makerOrder.totalAmount - amountSum * 2;        
    }
    //判断吃单状态是否为已完结，true：已完结，false：未完结
    function isCompleted(State state) internal pure returns (bool) {
        return state == State.BothCancelled || state == State.SellerConfirmed || state == State.SystemCanceled;
    }
    //计算实际支付总金额（误差控制在[-50, 50]美分），price为美分价格
    function calculateTotalValue(uint64 price, uint256 amount) internal returns (uint256) {
        uint256 result = price * amount + getLoopValue() - 50;
        return result;
    }
    //获取循环值，loopValue自增+1，超过100，赋值为0
    function getLoopValue () private returns (uint8){
        loopValue = loopValue + 1; 
        if (loopValue > 100){
            loopValue = 0;
        }
        return loopValue;
    }
    //是否移除指定的挂单，true：可移除，false：不可移除
    function needRemoveSpecificOrder(MakerOrder storage makerOrder) internal view returns(bool){
        if(makerOrder.price > 0){//说明挂单存在
            uint256 totalAmount = makerOrder.totalAmount;
            if(totalAmount < minAmountThreshold){//1、总数量小于最小阈值
                return true;
            }
            uint256 availableAmount = getAvailableAmount(makerOrder);
            if(availableAmount < minAmountThreshold / 2){//2、可购数量小于最小阈值的一半
                return true;
            }
            //2.超过吃单截止时间 （3天）  block.timestamp(10分钟出一个块，后续再给)
        }
        return false;
    }
    //挂单列表数量超过最大阈值, 删除优先级最高挂单(即单价最高，存在单价相同的话，删除更早的)
    function removeMaxOrder(uint size) internal {
        if(size >= maxSizeThreshold){   
            Heap.Node memory node = priorityQueue.extractMax();
            uint64 orderId = node.id;
            MakerOrder storage makerOrder = makerOrders[orderId];
            withdrawUnlockAll(makerOrder);     
            delete makerOrders[orderId];  
        }
    }
    //移除指定的挂单（比如：可售币数量太少了）
    function removeOrderIfNecessary(MakerOrder storage makerOrder, uint64 orderId) internal {
         if(needRemoveSpecificOrder(makerOrder)){
            priorityQueue.extractById(orderId);
            withdrawUnlockAll(makerOrder);     
            delete makerOrders[orderId];
        }
    }
    //生成订单号，自增+2
    function generateNewOrderId() internal returns (uint64){
        orderNo = orderNo + 2; 
        return orderNo;
    }
    //从合约地址往个人钱包转币
    function transfer2Account(address payable receiver, uint amount) internal {
        receiver.transfer(amount);  
    }
}