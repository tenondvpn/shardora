pragma solidity >=0.4.16 <0.9.0;       //指定编译器版本
 contract SimpleStorage {              //contract关键字表示一个合约，如果contract替换为library，则表示一个库
	uint storedData;                   //声明一个状态变量，类型uint，uint256的别名，256位无符号整数
    address public minter;            //声明一个状态变量，类型address，160位的值（一般看到16进制形式，长度40，示例：01cab67e8eca011d1ea49177807690fa5b9958c2），适合存储合约地址或外部人员的密钥对，且不允许任何算数操作
									  //public代表默认生成一个外部访问方法,  编译器自动实现，等价于 function minter() external view returns (address) { return minter; }
    mapping(address =>int64)  balances; //声明一个map的状态变量，key类型为address，value类型为int64，64为的有符号整数

    // constructor(uint256 x) {                   //关键字constructor声明的函数为构造函数， 有参构造函数
    //     storedData = x;
    // }
     constructor() {                           //无参构造函数
        storedData = 100;
    }
	function set(uint x) public {              //public代表可从外部访问  
		storedData =x;                         //状态变量赋值为x
	} 

    // function set(uint x) internal {              //internal代表不允许外部访问，但允许继承，  public > internal > private 
	// 	storedData =x;                         
	// } 

    //  function set(uint x) private {               //private代表即不允许外部访问，也不允许继承   
	// 	storedData =x;                         
	// }

	function get() public view returns (uint) {//view代表只获取状态变量的值，不修改状态变量; returns 代表有返回值，返回值类型为uint
 		return storedData;                      //获取状态变量的值
	}
} 
