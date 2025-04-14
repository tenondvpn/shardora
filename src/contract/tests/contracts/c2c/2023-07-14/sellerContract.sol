pragma solidity 0.4.24;
pragma experimental ABIEncoderV2;
import "./Heap.sol";

contract SellerContract{
    enum CancelState{ uncancel, buyerCanceled, sellerCanceled, bothCanceled}
    enum PaymentState{ unpayment, payment}
    enum PaymentConfirmState{ paymentUnConfirm, paymentConfirmed}
    uint constant minAmountThreshold = 1000; //低于该值不允许挂单出售
    uint constant maxSizeThreshold = 1000;      //挂单列表允许的最大长度

    using Heap for Heap.Data;
    Heap.Data public priorityQueue;  //优先级由单价和订单号共同决定（单价越高优先级越高，单价相同时，订单号越小优先级越高），另外订单号映射到挂单列表中挂单。
    address contractAddress;
    uint8 loopValue;//[0, 100] - 50，计算实际支付金额，卖家根据收款金额对应到订单 
    uint64 orderNo; //订单号，依次递增 
    mapping(uint64 => MakerOrder) public makerOrders;//挂单订单号至挂单订单映射
    //mapping(uint64=>TakerOrder) public takers; //吃单订单号至吃单订单映射

    //constructor(address _contractAddress) public {     
    constructor() public {
        priorityQueue.init(); 
       // contractAddress = _contractAddress;
        loopValue = 50;
        orderNo  = 0;
    }
    
    struct MakerOrder {
        address makerAddress;  //ZJB 个人钱包地址
        string nickname;       //昵称
        uint256 creditScore;    //信用分
        uint64 price;          //单价     （挂单时传入，始终不变）
        uint256 totalAmount;     //总售量  （挂单时传入，始终不变）
        uint256 availableAmount;  //可售数量（根据未锁定数量，重新计算可售数量）
        uint256 filledAmount;      //已售数量  （卖家确认收款后，累加售出数量）
        mapping(string => string[5]) paymentTypeBankCardNoMap;//支付方式和账号
        uint8 buyerMortgagePercentage;    //买家保证金比例，默认100%
        uint8 sellerMortgagePercentage;   //卖家保证金比例，默认100%
        uint256 unlockedAmount; //未锁定数量（操作赎回按钮赎回，买家或卖家的操作都可能改变该值，比如：卖家确认收款后，赎回部分保证金，未锁定数量增加）
        uint256 makerBlockHeight;            //挂单时块的高度 
        uint64 makerOrderId;                //挂单订单号
        mapping(uint64=>TakerOrder) takerOrders; //吃单订单号至吃单订单映射
    }

    struct TakerOrder {
        address takerAddress;   ///ZJB 个人钱包地址
        string nickname;           //昵称
        uint256 price;             //单价  	
        uint256 totalValue;        //实际支付金额（不完全等于单价乘以购买数量）
        uint256 amount;          //购买数量
        CancelState  cancelState;   //取消状态, 共4种
        PaymentState paymentState; //汇款状态，共2种
        PaymentConfirmState paymentConfirmState; //(卖家)确认收款状态，共2种
        uint256 startBlockHeight;       //吃单开始时块的高度
        uint64 takerOrderId;           //吃单订单号
        uint256 endBlockHeight;       //吃单结束时块的高度
    }

    mapping(string =>string[5])  paymentTypeBankCardNoMap;

    //创建一个新的挂单
    //function addMakerOrder(address makerAddress, uint64 price, uint64 totalAmount, mapping(string => string[5]) memory paymentTypeBankCardNoMap) public returns (uint){ //加锁或加可重入锁？
    function addMakerOrder(address makerAddress, uint64 price, uint256 totalAmount) public returns (uint){ //加锁或加可重入锁？
        if(precondition(price, totalAmount) == 0){ //前置条件检查
            return 0;
        }
        if(judgeRemoveOrder()){  //移除旧的挂单
            removeOrder();
        }
            
        //从个人钱包makerAddress往合约地址contractAddress转币
        if(true/*卖家交易金和保证金往合约地址transfer转币成功*/){
            uint makerBlockHeight = 1;                  //1、获取块的高度
            uint64 orderId = generateNewOrderId();      //2、生成订单号（挂单唯一标识）
            string memory nickname = "nickname";        //3、根据挂单者地址makerAddress查询昵称
            uint256 creditScore = 100;                  //4、根据挂单者地址makerAddress查询信用分
            MakerOrder memory makerOrder = MakerOrder(makerAddress, nickname, creditScore, price, totalAmount, 0, 0, 
                100, 100, 0, makerBlockHeight, orderId);
            makerOrders[orderId] = makerOrder;//新建挂单加入挂单列表
            refreshAmount(orderId, totalAmount);
            priorityQueue.insertWithId(price, orderId);//新建挂单加入优先队列(价格，订单号)
            return orderId;
        }else{
            return 0;
        }
    }   

    //创建一个吃单订单
    function addTakerOrder(uint64 makerOrderId, address takerAddress, uint256 amount) public returns (uint){   //加锁或加可重入锁？
        if(preconditionBeforeTakerOrder(makerOrderId, amount) == 0){ //前置条件检查
            return 0;
        }
        if(true/*买家保证金往合约地址transfer转币成功*/){  //从个人钱包takerAddress往合约地址contractAddress转币
            refreshAmount(makerOrderId, 0 - purchaseAmount2UnlockAmount(amount));
            uint startBlockHeight = 1;                  //1、获取块的高度
            uint64 takerOrderId = generateNewOrderId(); //2、生成订单号（挂单唯一标识）
            string memory nickname = "nickname";        //3、根据挂单者地址makerAddress查询昵称
            MakerOrder storage makerOrder = makerOrders[makerOrderId];
            uint256 totalValue = calculateTotalValue(makerOrderId, amount); //4、计算实际支付总金额totalValue （误差控制在[-50, 50]美分）
            makerOrder.takerOrders[takerOrderId] = TakerOrder(takerAddress, nickname, makerOrder.price, amount, totalValue, CancelState.uncancel, PaymentState.unpayment, 
            PaymentConfirmState.paymentUnConfirm, startBlockHeight, takerOrderId,0);
            return takerOrderId;
        }else{
        //    return 0;
        }
    }

    //买家确认汇款    
    function buyerPayment (uint64 makerOrderId, uint64 takerOrderId) returns (bool){

        return true;
    }

    function sellerPaymentConfirm (uint256 makerOrderId, uint256 takerOrderId) returns (uint){
	    if(true/*合约地址往买家地址transfer转币成功*/){
		 //1、计算未锁定币的数量unlockedAmount，新增保证金释放
        //2、重新计算可售数量calculateAvailableAmount
        }
    }

    ///取消订单
    function cancellOrder (uint256 makerOrderId, uint256 takerOrderId, CancelState cancelState) returns (bool) {
        if(true/*双方取消*/){
            if(true/*合约地址往买家地址transfer转币成功*/){
                    //1、计算未锁定币的数量unlockedAmount，新增交易金和保证金释放
            //2、重新计算可售数量calculateAvailableAmount
            }
        }
        return true;
    }

    //计算实际支付总金额
    function calculateTotalValue(uint64 makerOrderId, uint256 amount) returns (uint256) {
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        uint64 price = makerOrder.price;
        uint256 result = price * amount + getLoopValue() - 50; //注意美元/美分单位变换
        return result;
    }

    //获取循环值，loopValue自增+1，超过100，赋值为0
    function getLoopValue () returns (uint8){
        loopValue = loopValue + 1; 
        if (loopValue > 100){
            loopValue = 0;
        }
        return loopValue;
    }

    //吃单购买amount数量的币,挂单相应的扣减的未锁定币的数量
    function purchaseAmount2UnlockAmount(uint256 amount) public pure returns(uint256){
        return 2 * amount; //100%的保证金比例
    }

    function isExistEntry(uint64 orderId) public view returns(bool){
        return makerOrders[orderId].makerOrderId > 0;
    }

    //判断吃单订单是否完结(双方都取消或卖家确认收款)
    function isTakerOrderOver(uint64 makerOrderId, uint64 takerOrderId) public view returns(bool){
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        TakerOrder takerOrder = makerOrder.takerOrders[takerOrderId];
        if(uint(takerOrder.cancelState) == uint(CancelState.sellerCanceled)){
            return true;
        }
        if(uint(takerOrder.paymentConfirmState) == uint(PaymentConfirmState.paymentConfirmed)){
            return true;
        }
        if(takerOrder.paymentConfirmState == PaymentConfirmState.paymentConfirmed){
            return true;
        }
        return false;
    }

    //吃单前置条件检查，若失败返回0，否则返回1
    function preconditionBeforeTakerOrder(uint64 makerOrderId, uint256 amount) internal view returns (uint){
        if(amount < minAmountThreshold / 2){//1、小于最小出售阈值，不允许交易
            return 0;
        }
        if(amount % (minAmountThreshold / 2) != 0){//2、不是500的整数倍，不允许出售
            return 0;
        }
        if(!isExistEntry(makerOrderId)){ //3、未查询到对应的挂单
            return 0;
        }
        MakerOrder storage makerOrder = makerOrders[makerOrderId];
        uint availableAmount = makerOrder.availableAmount;
        mapping(uint64=>TakerOrder) takerOrders = makerOrder.takerOrders;
        if(true/*挂单中吃单者数量大于1000*/){//4、同一个挂单中吃单列表太长了
         //   return 0;
        }
        if(amount > availableAmount){ //5、购买数量大于挂单可售数量
             return 0; 
        }
        return 1;
    }

    //挂单前置条件检查，若失败返回0，否则返回1
    function precondition(uint64 price, uint256 totalAmount) internal view returns (uint){
        if(totalAmount < minAmountThreshold){//1、小于最小出售阈值，不允许出售
            return 0;
        }
        if(totalAmount % (minAmountThreshold / 2) != 0){//2、不是500的整数倍，不允许出售
            return 0;
        }
        uint size = priorityQueue.size();
        if(size >= maxSizeThreshold){
            Heap.Node memory head = priorityQueue.getMax();
            uint64 maxPrice = head.priority;
            if(price > maxPrice){//3、挂单列表已满，单价比优先队列中最高单价还大，不允许出售
                return 0;
            }
        }
        return 1;
    }

    //判断是否需要移除挂单，是返回true，否则返回false
    function judgeRemoveOrder() internal view returns (bool){
        uint size = priorityQueue.size();
        if(size >= maxSizeThreshold){ //超过最大阈值   
            return true;
        }else{
            return false;
        }
    }

    //删除挂单(即单价最高，存在单价相同的话，删除更早的)，同时赎回该挂单所有未锁定的币
    function removeOrder() internal returns (uint64){
        Heap.Node memory node = priorityQueue.extractMax(); //删除优先队列中的挂单
        uint64 orderId = node.id;
        MakerOrder storage maxOrder = makerOrders[orderId];
        revokeOrder(orderId, maxOrder.unlockedAmount); //赎回所有未锁定的币
        delete makerOrders[orderId]; //删除挂单列表中的挂单
    }

     //赎回指定数量币
    function revokeOrder (uint64 orderId, uint256 amount) public returns (uint){
        MakerOrder storage makerOrder = makerOrders[orderId];
        uint unLockedAmount = makerOrder.unlockedAmount;
        if(amount <= unLockedAmount){ //判断可赎回币数量是否足够
            if(true /*合约地址往卖家地址transfer转币成功)*/){
                refreshAmount(orderId, 0 - amount); //1、刷新币的数量
            }
        }
    }

    //刷新币的数量，unLockedAmount代表新增或减少未锁定币的数量 (unLockedAmount>0,则增加, unLockedAmount<0,则减少)
    function refreshAmount(uint64 orderId, uint256 unLockedAmount) internal{
        refreshUnlockedAmount(orderId, unLockedAmount);
        refreshAvailableAmount(orderId);  
    }

    //刷新未锁定币数量
    function refreshUnlockedAmount(uint64 orderId, uint256 unLockedAmount) internal returns (uint){
        MakerOrder storage makerOrder = makerOrders[orderId];
        makerOrder.unlockedAmount = makerOrder.unlockedAmount + unLockedAmount;
        return makerOrder.unlockedAmount;
    }

    //刷新可售币数量
    function refreshAvailableAmount(uint64 orderId) internal returns (uint){
        MakerOrder storage makerOrder = makerOrders[orderId];
        uint unLockedAmount = makerOrder.unlockedAmount;
        makerOrder.availableAmount = unLockedAmount / 2; //默认100%保证金计算的
        return makerOrder.availableAmount;
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

    function dump(uint64 orderId,uint64 takerOrderId) public view returns(TakerOrder){
        MakerOrder storage makerOrder = makerOrders[orderId];
        return makerOrder.takerOrders[takerOrderId];
    }
}