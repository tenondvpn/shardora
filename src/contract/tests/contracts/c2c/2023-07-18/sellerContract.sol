pragma solidity 0.4.24;
pragma experimental ABIEncoderV2;
import "./Heap.sol";

contract SellerContract{
    enum MakerOrderState{Active, Inactive} //挂单订单状态：活跃状态(即挂单列表中可见) 和 非活跃状态(挂单列表中不可见)， 挂单列表（Inactive）和订单列表（Active, Inactive）依赖该字段区分
    enum TakerOrderState{Incomplete, Completed} //吃单订单状态：订单交易未完成(买家未汇款或卖家未确认收款) 和 交易已完成(双方已取消 或 交易成功 或 强制释放锁定币)
    enum ActionState { MakerOrder, WithdrawUnlock, TakerOrder, CancelOrder, DealSuccess} //双方行为，挂单，赎回，吃单，取消，交易成功
    enum CancelState{ uncancel, buyerCanceled, sellerCanceled, bothCanceled} //吃单订单取消状态，4种， 未取消，买家取消，卖家取消，双方取消
    enum BuyerPaymentState{ unpayment, payment}                              //吃单订单买家汇款状态，2种，未汇款， 已汇款
    enum SellerPaymentConfirmState{ paymentUnConfirm, paymentConfirmed}      //吃单订单卖家收款状态，2种，未确认收款， 已确认收款
    uint constant minAmountThreshold = 1000;                                 //低于该值不允许挂单出售
    uint constant maxSizeThreshold = 1000;                                   //挂单列表允许的最大长度
    mapping(address=>uint)  tokens;//

    using Heap for Heap.Data;
    Heap.Data public priorityQueue;  //优先级由单价和订单号共同决定（单价越高优先级越高，单价相同时，订单号越小优先级越高），另外订单号映射到挂单列表中挂单。
    address contractAddress;
    uint8 loopValue;                //[0, 100] - 50，计算实际支付金额，卖家根据收款金额对应到订单 
    uint64 orderNo;                 //订单号，自增+1 
    mapping(uint64=>MakerOrder) public makerOrders;       //挂单列表，挂单订单号至挂单订单映射
    mapping(uint64=>MakerOrder) public removedMakerOrders;//移除后的挂单列表

    //constructor(address _contractAddress) public {     
    constructor() public {
        priorityQueue.init(); 
       // contractAddress = _contractAddress;
        loopValue = 50;
        orderNo  = 0;
    }
    
    struct MakerOrder {
        address makerAddress;   //ZJB 个人钱包地址
        string nickname;        //昵称
        uint256 creditScore;    //信用分
        uint64 price;           //单价    （挂单时传入，始终不变）
        uint256 totalAmount;    //总售量  （挂单时传入，始终不变）
        uint256 availableAmount;//可售数量（根据未锁定数量，计算得到）
        uint256 filledAmount;   //已售数量（双方交易成功后，累加出售数量）
        mapping(string => string[5]) paymentTypeBankCardNoMap;//支付方式和账号
        uint8 buyerMortgagePercentage; //买家保证金比例，默认100%
        uint8 sellerMortgagePercentage;//卖家保证金比例，默认100%
        uint256 unlockedAmount;        //未锁定数量（操作赎回按钮赎回，买家或卖家的操作都可能改变该值，比如：卖家确认收款后，赎回部分保证金，未锁定数量增加）
        //uint256 makerBlockHeight;    //挂单时块的高度 ??
        uint64 makerOrderId;           //挂单订单号
        mapping(uint64=>TakerOrder) takerOrders;//吃单订单号至吃单订单映射
        MakerOrderState makerOrderState;        //挂单订单状态：活跃状态 和 非活跃状态
    }

    struct TakerOrder {
        address takerAddress;    //ZJB 个人钱包地址
        string nickname;         //昵称
        uint256 price;           //单价  
        uint256 amount;          //交易金数量，即购买数量	
        uint256 totalValue;      //实际支付金额（不完全等于单价乘以购买数量）
        CancelState  cancelState;//取消状态, 共4种
        BuyerPaymentState paymentState;//汇款状态，共2种
        SellerPaymentConfirmState paymentConfirmState;//(卖家)确认收款状态，共2种
        uint256 startBlockHeight;       //吃单开始时块的高度？？
        uint64 takerOrderId;            //吃单订单号
        //uint256 endBlockHeight;       //吃单结束时块的高度
        TakerOrderState takerOrderState;//吃单订单状态：未完结 和 已完结
    }

    mapping(string =>string[5])  paymentTypeBankCardNoMap;

    //创建一个新的挂单
    //function addMakerOrder(address makerAddress, uint64 price, uint64 totalAmount, mapping(string => string[5]) memory paymentTypeBankCardNoMap) public returns (uint){ //加锁或加可重入锁？
    function addMakerOrder(address makerAddress, uint64 price, uint256 totalAmount) public returns (uint){//加锁或加可重入锁？
        if(!precondition4MakerOrder(price, totalAmount)){ 
            return 0;
        }
        if(needRemoveMaxOrder()){//移除旧的挂单
            removeMaxOrder();
        }
            
        //从个人钱包makerAddress往合约地址contractAddress转币
        if(true/*卖家交易金和保证金往合约地址transfer转币成功*/){
            uint makerBlockHeight = 1;                  //1、获取块的高度
            uint64 orderId = generateNewOrderId();      //2、生成订单号（挂单唯一标识）
            string memory nickname = "nickname";        //3、根据挂单者地址makerAddress查询昵称
            uint256 creditScore = 100;                  //4、根据挂单者地址makerAddress查询信用分
            MakerOrder memory makerOrder = MakerOrder(makerAddress, nickname, creditScore, price, totalAmount, 0, 0, 
                100, 100, 0, orderId, MakerOrderState.Active);
            //MakerOrder memory makerOrder = MakerOrder({makerAddress:makerAddress, nickname:nickname, creditScore:creditScore, price:price, totalAmount:totalAmount, availableAmount:0, filledAmount:0, buyerMortgagePercentage:100, sellerMortgagePercentage:100, unlockedAmount:0, takerOrderId:orderId, state:MakerOrderState.Active});
            makerOrders[orderId] = makerOrder;          //新建挂单加入挂单列表
            refreshMakerOrderAmount(orderId, calculateAvailableAmount(orderId), ActionState.MakerOrder);  
            priorityQueue.insertWithId(price, orderId);//新建挂单加入优先队列(价格，订单号)
            return orderId;
        }else{
            return 0;
        }
    }   

    //创建吃单订单
    function addTakerOrder(uint64 makerOrderId, address takerAddress, uint256 amount) public returns (uint){//加锁或加可重入锁？
        if(!precondition4TakerOrder(makerOrderId, amount)){ 
            return 0;
        }
        if(true/*买家保证金往合约地址transfer转币成功*/){  //从个人钱包takerAddress往合约地址contractAddress转币
            refreshMakerOrderAmount(makerOrderId, amount, ActionState.TakerOrder); 
            uint startBlockHeight = 1;                  //1、获取块的高度？
            uint64 takerOrderId = generateNewOrderId(); //2、生成订单号（挂单唯一标识）
            string memory nickname = "nickname";        //3、根据挂单者地址takerAddress查询昵称，中心化数据库服务器？
            MakerOrder storage makerOrder = getMakerOrder(makerOrderId);
            uint64 price = makerOrder.price;
            uint256 totalValue = calculateTotalValue(price, amount);//4、计算实际支付总金额
            makerOrder.takerOrders[takerOrderId] = TakerOrder(takerAddress, nickname, makerOrder.price, amount, totalValue, CancelState.uncancel, BuyerPaymentState.unpayment, 
            SellerPaymentConfirmState.paymentUnConfirm, startBlockHeight, takerOrderId, TakerOrderState.Incomplete);
            removeOrderIfNecessary(makerOrderId);
            return takerOrderId;
        }else{
        //    return 0;
        }
    }

    //买家操作汇款状态为“已汇款”    
    function buyerPayment (uint64 makerOrderId, uint64 takerOrderId) public returns (bool){
         if(!precondition4BuyerPayment(makerOrderId, takerOrderId)){
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId ,takerOrderId);
        takerOrder.paymentState = BuyerPaymentState.payment;  
        return true;
    }

    //卖家操作"确认收款"
    function sellerPaymentConfirm (uint64 makerOrderId, uint64 takerOrderId) public returns (bool){
	   if(!precondition4PaymentConfirm(makerOrderId, takerOrderId)){ 
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId ,takerOrderId);
       
        if(true/*合约地址往买家地址transfer转币成功*/){
            takerOrder.paymentConfirmState = SellerPaymentConfirmState.paymentConfirmed; 
            takerOrder.takerOrderState = TakerOrderState.Completed;
            uint256 amount = takerOrder.amount;
            refreshMakerOrderAmount(makerOrderId, amount, ActionState.DealSuccess); 
        }
        removeOrderIfNecessary(makerOrderId);
        return true;
    }

    //买家或卖家取消订单 （需要权限校验？ 即买家只能取消买家取消）
    function cancelOrder (uint64 makerOrderId, uint64 takerOrderId, CancelState cancelState) public returns (bool) {
        if(!precondition4CancelOrder(makerOrderId, takerOrderId, cancelState)){
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId, takerOrderId);
        if(cancelState == CancelState.bothCanceled/*双方取消*/){
            if(true/*合约地址往买家地址transfer转币成功*/){
                takerOrder.cancelState = cancelState;
                takerOrder.takerOrderState = TakerOrderState.Completed;
                uint256 amount = takerOrder.amount;
                refreshMakerOrderAmount(makerOrderId, amount, ActionState.CancelOrder); 
            }
        }else{ //买家或卖家之一取消订单
            takerOrder.cancelState = cancelState;
        }
        removeOrderIfNecessary(makerOrderId);
        return true;
    }

    //赎回指定数量的未锁定币
    function withdrawUnlock (uint64 orderId, uint256 amount) public returns (bool){
        if(!precondition4WithdrawUnlock(orderId, amount)){
            return false;
        }
        if(true /*合约地址往卖家地址transfer转币成功)*/){
            refreshMakerOrderAmount(orderId, amount, ActionState.WithdrawUnlock); 
        }
        removeOrderIfNecessary(orderId);  
        return true;
    }

    //挂单前置条件检查，失败返回false，成功返回true
    function precondition4MakerOrder(uint64 price, uint256 totalAmount) internal view returns (bool){
        if(totalAmount < minAmountThreshold){        //1、小于最小出售阈值，不允许出售
            return false;
        }
        if(totalAmount % (minAmountThreshold/2) != 0){//2、不是500的整数倍，不允许出售
            return false;
        }
        uint size = priorityQueue.size();
        if(size >= maxSizeThreshold){
            Heap.Node memory head = priorityQueue.getMax();
            uint64 maxPrice = head.priority;
            if(price > maxPrice){                    //3、挂单列表已满，单价比优先队列中最高单价还大，不允许出售
                return false;
            }
        }
        if(true/*用户active挂单数量超过100个*/){  //4、用户活跃挂单数量太多了，或限制用户一段时间内的挂单数量（active和inactive）？
            //return false;
        }
        return true;
    }

    //吃单前置条件检查，若失败返回0，否则返回1
    function precondition4TakerOrder(uint64 makerOrderId, uint256 amount) internal view returns (bool){
        if(amount < minAmountThreshold / 2){//1、小于最小出售阈值，不允许交易
            return false;
        }
        if(amount % (minAmountThreshold / 2) != 0){//2、不是500的整数倍，不允许出售
            return false;
        }
        if(!isExistMakerOrder(makerOrderId)){ //3、未查询到对应的挂单， 订单列表不可见？
            return false;
        }
        MakerOrder storage makerOrder = getMakerOrder(makerOrderId);
        uint availableAmount = makerOrder.availableAmount;
        mapping(uint64=>TakerOrder) takerOrders = makerOrder.takerOrders;
        if(true/*挂单中吃单者数量大于1000*/){//4、同一个挂单中吃单列表太长了
         //   return false;
        }
        if(amount > availableAmount){ //5、购买数量大于挂单可售数量
             return false; 
        }
        return true;
    }

    //买家已汇款前的条件检查，失败返回false，成功返回true
    function precondition4BuyerPayment(uint64 makerOrderId, uint64 takerOrderId) internal view returns (bool){
        if(!isExistTakerOrder(makerOrderId, takerOrderId)){  
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId, takerOrderId);
        if(takerOrder.paymentState == BuyerPaymentState.payment){ //买家操作过"已汇款"
            return false;
        }
        return true;
    }

     //卖家确认收款前的条件检查，失败返回false，成功返回true
    function precondition4PaymentConfirm(uint64 makerOrderId, uint64 takerOrderId) internal view returns (bool){
        if(!preconditionBeforeHandle(makerOrderId, takerOrderId)){
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId, takerOrderId);
        if(takerOrder.paymentConfirmState == SellerPaymentConfirmState.paymentConfirmed){ //卖家操作过"确认收款"
            return false;
        }
        return true;
    }

    //买家或卖家取消订单前的条件检查，失败返回false，成功返回true
    function precondition4CancelOrder(uint64 makerOrderId, uint64 takerOrderId, CancelState cancelState) internal view returns (bool){
        if(!preconditionBeforeHandle(makerOrderId, takerOrderId)){ 
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId, takerOrderId);
        if(takerOrder.cancelState == cancelState){ //已取消过
            return false;
        }
        return true;
    }

    //赎回未锁定的币前的条件检查，失败返回false，成功返回true
    function precondition4WithdrawUnlock(uint64 orderId, uint256 amount) internal view returns (bool){
        if(!isExistMakerOrder(orderId)){ 
            return false;
        }
        if(amount <= 0){
            return false;
        }
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        uint unLockedAmount = makerOrder.unlockedAmount;
        if(amount > unLockedAmount){ //赎回币数量大于未锁定币数量  (卖家作恶？)
            return false;
        }
        return true;
    }

      //操作的前置条件检查，失败返回false，成功返回true
    function preconditionBeforeHandle(uint64 makerOrderId, uint64 takerOrderId) internal view returns (bool){
        if(!isExistTakerOrder(makerOrderId, takerOrderId)){
            return false;
        }
        TakerOrder storage takerOrder = getTakerOrder(makerOrderId, takerOrderId);
        if(takerOrder.takerOrderState == TakerOrderState.Completed){ //吃单订单状态:已完结
            return false;
        }
        return true;
    }

     //获取挂单订单      
    function getMakerOrder(uint64 orderId) internal view returns (MakerOrder storage){
        return makerOrders[orderId];  
    }

    //获取吃单订单    
    function getTakerOrder(uint64 makerOrderId, uint64 takerOrderId) internal view returns (TakerOrder storage){
        MakerOrder storage makerOrder = getMakerOrder(makerOrderId);
        return makerOrder.takerOrders[takerOrderId];
    }

     //删除挂单订单, 订单列表中数据如何获取？      
    function deleteMakerOrder(uint64 orderId) internal returns (bool){
        if(isExistMakerOrder(orderId)){
            delete makerOrders[orderId]; 
            return true;  
        }
        return false;
    }

    //计算实际支付总金额,（误差控制在[-50, 50]美分）
    function calculateTotalValue(uint64 price, uint256 amount) internal returns (uint256) {
        uint256 result = price * amount + getLoopValue() - 50; //注意美元/美分单位变换
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

    //挂单存在返回true，不存在返回false
    function isExistMakerOrder(uint64 orderId) public view returns(bool){
        return makerOrders[orderId].makerOrderId > 0;
    }

    //吃单存在返回true，不存在返回false
    function isExistTakerOrder(uint64 makerOrderId, uint64 takerOrderId) public view returns(bool){
        if(isExistMakerOrder(makerOrderId)){
            MakerOrder storage makerOrder = getMakerOrder(makerOrderId);
            return makerOrder.takerOrders[takerOrderId].takerOrderId > 0;
        }
        return false;
    }

    //是否移除优先级最高的挂单(价格最高)，是返回true，否则返回false
    function needRemoveMaxOrder() internal view returns (bool){
        uint size = priorityQueue.size();
        return size >= maxSizeThreshold; //超过最大阈值   
    }

     //是否移除指定的挂单
    function needRemoveSpecificOrder(uint64 orderId) internal view returns(bool){
        if(isExistMakerOrder(orderId)){
            MakerOrder storage makerOrder = getMakerOrder(orderId);
            uint256 availableAmount = makerOrder.availableAmount;
            if(availableAmount < minAmountThreshold / 2){//1、小于最小出售阈值，不允许交易
                return true;
            }
            if(makerOrder.makerOrderState == MakerOrderState.Inactive){//2、挂单非活跃状态
                return true;
            }
            //3.超过吃单截止时间 （3天）
            //4、锁定币超过3个月
        }
        return false;
    }

    //删除优先级最高挂单(即单价最高，存在单价相同的话，删除更早的)
    function removeMaxOrder() internal returns (uint64){
        Heap.Node memory node = priorityQueue.extractMax(); //删除优先队列中的挂单
        uint64 orderId = node.id;
        removeOrderMandatory(orderId);
    }

    //强制移除挂单，同时赎回该挂单所有未锁定的币
    function removeOrderMandatory(uint64 orderId) internal returns (uint64){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        withdrawUnlock(orderId, makerOrder.unlockedAmount);     //赎回所有未锁定的币
        if(0 == makerOrder.unlockedAmount && makerOrder.makerOrderState == MakerOrderState.Active){//无可赎回币   判断多余?? 
            makerOrder.makerOrderState = MakerOrderState.Inactive;
        }
    }

     //根据情况决定移除挂单（比如：可售币数量太少了），同时赎回该挂单所有未锁定的币
    function removeOrderIfNecessary(uint64 orderId) internal returns (uint64){
         if(needRemoveSpecificOrder(orderId)){
            removeOrderMandatory(orderId);
        }
    }

    //刷新挂单币的数量，amount为交易金数量
    function refreshMakerOrderAmount(uint64 orderId, uint256 amount, ActionState action) internal{
        uint256  unlockAmount;
        if(action == ActionState.MakerOrder){
            unlockAmount = amount + calculateSellerMortgage(orderId, amount);      //1、挂单，交易金和保证金都未锁定，增加未锁定量
        }else if(action == ActionState.TakerOrder){
            unlockAmount = 0 -(amount + calculateSellerMortgage(orderId, amount)); //2、吃单，锁定卖家交易金和保证金，减少未锁定量
        }else if(action == ActionState.CancelOrder){
            unlockAmount = amount + calculateSellerMortgage(orderId, amount);      //3、取消，释放卖家交易金和保证金，增加未锁定量
        }else if(action == ActionState.WithdrawUnlock){
            unlockAmount = 0 - amount;                                             //4、赎回，               直接减少未锁定量
        }else if(action == ActionState.DealSuccess){
            unlockAmount = calculateSellerMortgage(orderId, amount);               //5、交易成功，释放卖家保证金， 增加未锁定量
        }
        refreshUnlockedAmount(orderId, unlockAmount); 
        refreshAvailableAmount(orderId);              
        if(action == ActionState.DealSuccess){
            refreshFilledAmount(orderId, amount);     
        }
    }

    //根据交易金数量(amount)计算卖家保证金数量
    function calculateSellerMortgage(uint64 orderId, uint256 amount) internal view returns(uint256){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        uint256 mortgage = amount * (makerOrder.sellerMortgagePercentage / 100); 
        return mortgage;
    }

    //根据交易金数量(amount)计算买家保证金数量
    function calculateBuyerMortgage(uint64 orderId, uint256 amount) internal view returns(uint256){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        uint256 mortgage = amount * (makerOrder.buyerMortgagePercentage / 100); 
        return mortgage;
    }

    //刷新挂单未锁定币的数量 (unLockedAmount>0,则增加, unLockedAmount<0,则减少)
    function refreshUnlockedAmount(uint64 orderId, uint256 unLockedAmount) internal returns (uint){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        makerOrder.unlockedAmount = makerOrder.unlockedAmount + unLockedAmount;
        return makerOrder.unlockedAmount;
    }

    //根据未锁定币数量刷新可售币数量
    function refreshAvailableAmount(uint64 orderId) internal returns (uint){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        uint unLockedAmount = makerOrder.unlockedAmount;
        makerOrder.availableAmount = unLockedAmount * 100 / (makerOrder.sellerMortgagePercentage + 100);
        return makerOrder.availableAmount;
    }

    //根据币总量计算可售币数量（交易金量）
    function calculateAvailableAmount(uint64 orderId) internal view returns(uint256){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        uint256 availableAmount = makerOrder.totalAmount * 100 / (makerOrder.sellerMortgagePercentage + 100);
        return availableAmount;
    }

     //刷新已售币数量(仅交易成功达成这种场景下)
    function refreshFilledAmount(uint64 orderId, uint256 buyerPurchaseAmount) internal returns (uint){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        makerOrder.filledAmount = makerOrder.filledAmount + buyerPurchaseAmount;
        return makerOrder.filledAmount;
    }

    //生成订单号，自增+1
    function generateNewOrderId() internal returns (uint64){//可重入锁？
        orderNo = orderNo + 1; 
        return orderNo;
    }

    function size() public view returns (uint256){
        return priorityQueue.size();
    }

    function dump() public view returns(Heap.Node[] memory){
        return priorityQueue.dump();
    }

    function dump(uint64 orderId, uint64 takerOrderId) public view returns(TakerOrder){
        MakerOrder storage makerOrder = getMakerOrder(orderId);
        return makerOrder.takerOrders[takerOrderId];
    }
}