pragma solidity >=0.4.22 <0.8.0;

contract ByteStringsol {

    //创建两个类型
    string name = "hongweijiang";
    
    bytes name2 = new bytes(2);
   
    function getNameLength() public view returns(uint) {
        return bytes(name).length;
    }
     
      function getName() public view returns(uint) {
        return bytes(name).length;
    }

    function changeName()  public view returns(bytes memory) {
       return bytes(name);
    }
    // 修改name的值
     function changeName1() public {
       bytes(name)[1] = 'Y'; //修改name
    }
     function getName1() public view returns(string memory) {
       return name;  // hYngweijiang
    }
     // 初始化name2 的值
      function init() public {
       name2[0] =0x7a;
       name2[1] =0x68;
    }
        // 将name2 bytes类型强制转换为string类型
       function getName2() public view returns(string memory) {
       return string (name2);   //zh
    }

    
}