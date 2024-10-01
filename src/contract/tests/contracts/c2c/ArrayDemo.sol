// SPDX-License-Identifier: GPL-3.0
pragma solidity ^0.8.0;
import "./LibArrayForUint256Utils.sol";
contract ArrayDemo {

    uint[] public array;
    uint[] public array1;
    uint[] public array2;

    function binarySearch() public  returns (bool, uint256){  //二分查找
        array=new uint[](0);
        LibArrayForUint256Utils.addValue(array,2);
        LibArrayForUint256Utils.addValue(array,3);
         (bool result,uint256 mid) = LibArrayForUint256Utils.binarySearch(array,3);
        return (result, mid);
    }

    function firstIndexOf() public  returns (bool, uint256){  //查找第一个value值
        array=new uint[](0);
        LibArrayForUint256Utils.addValue(array,2);
        LibArrayForUint256Utils.addValue(array,3);
         (bool result,uint256 mid) = LibArrayForUint256Utils.firstIndexOf(array,3);
        return (result, mid);
    }

      function reverse() public  returns (uint[] memory){  //翻转
        array=new uint[](0);
        LibArrayForUint256Utils.addValue(array,2);
        LibArrayForUint256Utils.addValue(array,3);
        LibArrayForUint256Utils.reverse(array);
        return array;
    }

    function equals() public  returns (bool, bool){ //去重
        array=new uint[](2);
        array[0]=2;
        array[1]=2;
        array1=new uint[](2);
        array1[0]=2;
        array1[1]=2;
        array2=new uint[](2);
        array2[0]=2;
        array2[1]=3;
        bool result = LibArrayForUint256Utils.equals(array, array1);
        bool result2 = LibArrayForUint256Utils.equals(array, array2);
        return (result, result2);
    }

     function removeByIndex() public  returns (uint[] memory){ //根据索引位置删除
        array=new uint[](0);
        LibArrayForUint256Utils.addValue(array,2);
        LibArrayForUint256Utils.addValue(array,3);
        LibArrayForUint256Utils.removeByIndex(array,1);
        return array;
    }

    function removeByValue() public  returns (uint[] memory){ //根据值位置删除
        array=new uint[](0);
        LibArrayForUint256Utils.addValue(array,2);
        LibArrayForUint256Utils.addValue(array,3);
        LibArrayForUint256Utils.removeByValue(array,2);
        return array;
    }

    function addValue() public  returns (uint[] memory){   //添加值
        array=new uint[](0);
        // array add element 2
        LibArrayForUint256Utils.addValue(array,2);
        // array: {2}
        return array;
    }

    function extend() public  returns (uint[] memory){  //合并到array1
        array1=new uint[](2);
        array2=new uint[](2);
        LibArrayForUint256Utils.extend(array1,array2);
        // array1 length 4
        return array;
    }


    function qsort() public  returns (uint[] memory){ //去重
        array=new uint[](3);
        array[0]=2;
        array[1]=4;
        array[2]=3;
        LibArrayForUint256Utils.qsort(array);
        return array;
    }
    
    function max() public  returns (uint256 , uint256){
        array=new uint[](3);
        array[0]=2;
        array[1]=4;
        array[2]=3;
        (uint256 maxValue, uint256 maxIndex)  = LibArrayForUint256Utils.max(array);
        return  ( maxValue, maxIndex);
    }

    function min() public  returns (uint256 , uint256){
        array=new uint[](3);
        array[0]=2;
        array[1]=4;
        array[2]=3;
        (uint256 maxValue, uint256 maxIndex)  = LibArrayForUint256Utils.min(array);
        return  ( maxValue, maxIndex);
    }
   
    function size() public  returns (uint256 ){
        array=new uint[](0);
        LibArrayForUint256Utils.addValueRepeatedly(array,2); 
        LibArrayForUint256Utils.addValueRepeatedly(array,3);
        LibArrayForUint256Utils.addValueRepeatedly(array,2); //允许重复添加相同的元素
        return  array.length;    //长度为3
    }

     function removeByValueDisorder() public  returns (uint[] memory){ //根据值位置删除
        array=new uint[](0);
        LibArrayForUint256Utils.addValue(array,2);
        LibArrayForUint256Utils.addValue(array,3);
        LibArrayForUint256Utils.addValue(array,1);
        LibArrayForUint256Utils.removeByValueDisorder(array,2);
        return array;
    }
}