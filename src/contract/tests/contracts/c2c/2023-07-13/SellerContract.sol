pragma solidity 0.4.24;
pragma experimental ABIEncoderV2;
import "./Heap.sol";


contract SellerContract{
    enum CancelState{ uncancel, buyerCanceled, sellerCanceled, bothCanceled}
    enum PaymentState{ unpayment, payment}
    enum PaymentConfirmState{ paymentUnConfirm, paymentConfirmed}

    using Heap for Heap.Data;
    Heap.Data public priorityQueue;  //优先级由单价和订单号共同决定（单价越高优先级越高，单价相同时，订单号越小优先级越高），另外订单号映射到挂单列表中挂单。
    address contractAddress;
    uint randomValue;//[-50, 50]，计算实际支付金额，卖家根据收款金额对应到订单 
    uint64 orderId; //订单号，初始值为1，然后依次递增 
    mapping(uint64 => MakerOrder) public makerOrders;//挂单订单号至挂单订单映射
    
    //constructor(address _contractAddress) public {     
    constructor() public {
        priorityQueue.init(); 
       // contractAddress = _contractAddress;
        randomValue = 0;
        orderId  = 0;
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
        uint8 buyerMortgagePercentage;    //买家保证金，默认100%
        uint8 sellerMortgagePercentage;   //卖家保证金，默认100%
        uint256 unlockedAmount; //未锁定数量（操作赎回按钮赎回，买家或卖家的操作都可能改变该值，比如：卖家确认收款后，赎回部分保证金，未锁定数量增加）
        uint256 makerBlockHeight;            //挂单时块的高度 
        uint256 makerOrderId;                //挂单订单号
        mapping(uint256 => TakerOrder)  takerOrders; //吃单订单号至吃单订单映射
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
        uint256 takerOrderId;           //吃单订单号
        uint256 endBlockHeight;       //吃单结束时块的高度
    }

    mapping(string => string[5])  paymentTypeBankCardNoMap;

    address makerAddress;
    //创建一个新的挂单
    //function addMakerOrder(address makerAddress, uint64 price, uint64 totalAmount, mapping(string => string[5]) memory paymentTypeBankCardNoMap) public returns (uint){ //加锁或加可重入锁？
    function addMakerOrder(address makerAddress, uint64 price, uint256 totalAmount) public returns (uint){ //加锁或加可重入锁？
        if(totalAmount < 1000 || totalAmount % 500 != 0){
            return 0;
        }
            
        uint size = priorityQueue.size();
        if(size < 1){
            //1、获取当前块的高度makerBlockHeight
            uint makerBlockHeight = 1;
            //2、generateNewOrderId生成挂单订单号makerOrderId（挂单唯一标识）
            uint64 makerOrderId = generateNewOrderId();
            //3、根据挂单者地址makerAddress查询昵称nickname和信用分creditScore
            string memory nickname = "nickname";
            uint256 creditScore = 100;
            uint availableAmount = (totalAmount / 2);
            //6、从个人钱包makerAddress往合约地址contractAddress转币，即transfer操作，数量为totalAmount
            // if(卖家交易金和保证金往合约地址transfer转币成功){
                
                MakerOrder memory makerOrder = MakerOrder(makerAddress, nickname, creditScore, price, totalAmount, 0, 0, 
                    100, 100, totalAmount, makerBlockHeight, makerOrderId);
                makerOrders[makerOrderId] = makerOrder;
                
                //刷新当前可售数量availableAmount 
                refreshAvailableAmount(makerOrderId);
                //将挂单也加入优先队列(价格，订单号)
                priorityQueue.insertWithId(price, makerOrderId);
                return makerOrderId;
            // }else{
            //     return 0;
            // }
        }else{ //挂单列表达最大长度
            Heap.Node memory headNode = priorityQueue.getMax();
            uint64 headPrice = headNode.priority;
            uint64 headOrderId = headNode.id;
            if(price > headPrice){
                return 0;
            }else{
              removeMaxPrice();
            }

        }

    }   

    //删除优先队列中价格最高的 以及挂单列表中对应的挂单，同时赎回该挂单所有未锁定的币
    function removeMaxPrice() internal returns (uint64){
        Heap.Node memory headNode = priorityQueue.extractMax();
        uint64 headPrice = headNode.priority;
        uint64 headOrderId = headNode.id;
        MakerOrder  headOrder = makerOrders[headOrderId];
        //赎回所有的币
        revokeOrder (headOrderId, headOrder.unlockedAmount);
    }

     //挂单赎回指定数量币(amount)
    function revokeOrder (uint64 makerOrderId, uint256 amount) returns (uint){
        MakerOrder makerOrder = makerOrders[makerOrderId];
        uint unLockedAmount = makerOrder.unlockedAmount;

        if(amount < unLockedAmount){ //说明可赎回币数量足够
            if(true /*合约地址往卖家地址transfer转币成功)*/){
                //1、重新计算未锁定币的数量unlockedAmount，卖家提取币
                refreshAvailableAmount(makerOrderId); //2、重新计算可售数量calculateAvailableAmount
            }
        }
    }



    //根据保证金比例计算当前可售数量（默认100%）
    function refreshAvailableAmount(uint64 makerOrderId) returns (uint){
        MakerOrder makerOrder = makerOrders[makerOrderId];
        uint unLockedAmount = makerOrder.unlockedAmount;
        uint availableAmount = unLockedAmount / 2;
        makerOrder.availableAmount = availableAmount;
        return availableAmount;
    }

    //生成订单号，每次都在原有基础上+1
    function generateNewOrderId() internal returns (uint64){
        orderId = orderId + 1; 
        return orderId;
    }

    function size() returns (uint256){
        uint size = priorityQueue.size();
        return size;
    }

    function dump() public view returns(Heap.Node[] memory){
        return priorityQueue.dump();
    }
}