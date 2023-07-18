pragma solidity 0.4.24;
pragma experimental ABIEncoderV2;
import "./Heap.sol";

// this is a simple contract that uses the heap library.
// but it allows all data in the heap to be inserted and removed by anyone in the world!
// so you wouldnt write your contract like this, but it shows how to interactive with 
// the heap library. specifically you might use the "view" functions from below, but the 
// insert/extractMax/extractById functions you probably would put inside restrictive logic
contract PublicHeap{
  using Heap for Heap.Data;
  Heap.Data public data;

  constructor() public { data.init(); }

  function heapify(uint64[] memory priorities ) public {
    for(uint i ; i < priorities.length ; i++){
      data.insert(priorities[i]);
    }
  }
  function insert(uint64 priority) public returns(Heap.Node memory){
    return data.insert(priority);
  }
  //chengzongxing  新增insertWithId函数，id一方面映射到其它object对象，另一方面id在priority相同时，id越小优先级越高（insert函数和insertWithId函数仅可使用其中之一） 
  function insertWithId(uint64 priority,uint64 id) public returns(Heap.Node memory){
    return data.insertWithId(priority, id);
  }
  function extractMax() public returns(Heap.Node memory){
    return data.extractMax();
  }
  function extractById(uint64 id) public returns(Heap.Node memory){
    return data.extractById(id);
  }
  //view
  function dump() public view returns(Heap.Node[] memory){
    return data.dump();
  }
  function getMax() public view returns(Heap.Node memory){
    return data.getMax();
  }
  function getById(uint64 id) public view returns(Heap.Node memory){
    return data.getById(id);
  }
  function getByIndex(uint i) public view returns(Heap.Node memory){
    return data.getByIndex(i);
  }
  function size() public view returns(uint){
    return data.size();
  }
  function idCount() public view returns(uint64){
    return data.idCount;
  }
  function indices(uint64 id) public view returns(uint){
    return data.indices[id];
  }

  //测试连续count次入队
  function testInsert(uint count) public {
    for(uint i= 1; i <= count; i++){
      uint64 priority = random(10);
      Heap.Node memory node = data.insert(priority);
    }
  }

  //测试连续count次出队 （最高优先级出队）
  function testExtract(uint count) public returns (Heap.Node[] memory) {
    Heap.Node[] nodes;
    for(uint i= 1; i <= count; i++){
      Heap.Node memory node = data.extractMax();
      nodes.push(node);
    }
    return nodes;
  }

  uint randNonce = 0;
  //chengzongxing  生成一个0到number的随机数   (此方法容易被攻击)
   function random(uint64 number) internal returns(uint64) {
      randNonce++;
      return uint64(keccak256(now, msg.sender, randNonce)) % number;
  }
 

}