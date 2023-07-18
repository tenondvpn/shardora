pragma solidity 0.4.24;

// Eth Heap
// Author: Zac Mitton
// License: MIT

library Heap{ // default max-heap

  uint constant ROOT_INDEX = 1;

  struct Data{
    uint64 idCount;
    Node[] nodes; // root is index 1; index 0 not used
    mapping (uint64 => uint) indices; // unique id => node index
  }
  struct Node{
    uint64 id; //use with another mapping to store arbitrary object types
    uint64 priority;
  }

  //call init before anything else
  function init(Data storage self) internal{
    if(self.nodes.length == 0) self.nodes.push(Node(0,0));
  }

  function insert(Data storage self, uint64 priority) internal returns(Node){//√
    if(self.nodes.length == 0){ init(self); }// test on-the-fly-init
    self.idCount++;
    self.nodes.length++;
    Node memory n = Node(self.idCount, priority);
    _bubbleUp(self, n, self.nodes.length-1);
    return n;
  }

  //chengzongxing  新增insertWithId函数，id在priority相同时，id越小优先级越高（insert函数和insertWithId函数仅可使用其中之一） 
  function insertWithId(Data storage self, uint64 priority, uint64 id) internal returns(Node){//√
    if(self.nodes.length == 0){ init(self); }// test on-the-fly-init
    self.nodes.length++;
    Node memory n = Node(id, priority);
    _bubbleUp(self, n, self.nodes.length-1);
    return n;
  }

  function extractMax(Data storage self) internal returns(Node){//√
    return _extract(self, ROOT_INDEX);
  }
  function extractById(Data storage self, uint64 id) internal returns(Node){//√
    return _extract(self, self.indices[id]);
  }

  //view
  function dump(Data storage self) internal view returns(Node[]){
  //note: Empty set will return `[Node(0,0)]`. uninitialized will return `[]`.
    return self.nodes;
  }
  function getById(Data storage self, uint64 id) internal view returns(Node){
    return getByIndex(self, self.indices[id]);//test that all these return the emptyNode
  }
  function getByIndex(Data storage self, uint i) internal view returns(Node){
    return self.nodes.length > i ? self.nodes[i] : Node(0,0);
  }
  function getMax(Data storage self) internal view returns(Node){
    return getByIndex(self, ROOT_INDEX);
  }
  function size(Data storage self) internal view returns(uint){
    return self.nodes.length > 0 ? self.nodes.length-1 : 0;
  }
  function isNode(Node n) internal pure returns(bool){ return n.id > 0; }

  //private
  function _extract(Data storage self, uint i) private returns(Node){//√
    if(self.nodes.length <= i || i <= 0){ return Node(0,0); }

    Node memory extractedNode = self.nodes[i];
    delete self.indices[extractedNode.id];

    Node memory tailNode = self.nodes[self.nodes.length-1];
    self.nodes.length--;

    if(i < self.nodes.length){ // if extracted node was not tail
      _bubbleUp(self, tailNode, i);
      _bubbleDown(self, self.nodes[i], i); // then try bubbling down
    }
    return extractedNode;
  }
  function _bubbleUp(Data storage self, Node memory n, uint i) private{//√
    //chengzongxing  下面一行替换下下一行，priority优先级相等的时候，id(挂单订单号)越大，优先级越小 
    if(i==ROOT_INDEX || (n.priority < self.nodes[i/2].priority || (n.priority == self.nodes[i/2].priority && n.id > self.nodes[i/2].id))){
    //if(i==ROOT_INDEX || n.priority <= self.nodes[i/2].priority){
      _insert(self, n, i);
    }else{
      _insert(self, self.nodes[i/2], i);
      _bubbleUp(self, n, i/2);
    }
  }
  function _bubbleDown(Data storage self, Node memory n, uint i) private{//
    uint length = self.nodes.length;
    uint cIndex = i*2; // left child index

    if(length <= cIndex){
      _insert(self, n, i);
    }else{
      Node memory largestChild = self.nodes[cIndex];
      //chengzongxing  下面一行替换下下一行，priority优先级相等的时候，id(挂单订单号)越小，优先级越大 
      if(length > cIndex+1 && (self.nodes[cIndex+1].priority > largestChild.priority || (self.nodes[cIndex+1].priority == largestChild.priority && self.nodes[cIndex+1].id < largestChild.id))){
      //if(length > cIndex+1 && self.nodes[cIndex+1].priority > largestChild.priority ){
        largestChild = self.nodes[++cIndex];// TEST ++ gets executed first here
      }
      //chengzongxing  下面一行替换下下一行，priority优先级相等的时候，id(挂单订单号)越大，优先级越小 
      if(largestChild.priority < n.priority ||(largestChild.priority == n.priority && largestChild.id > n.id)){ //TEST: priority 0 is valid! negative ints work
      //if(largestChild.priority <= n.priority){ //TEST: priority 0 is valid! negative ints work
        _insert(self, n, i);
      }else{
        _insert(self, largestChild, i);
        _bubbleDown(self, n, cIndex);
      }
    }
  }

  function _insert(Data storage self, Node memory n, uint i) private{//√
    self.nodes[i] = n;
    self.indices[n.id] = i;
  }

 
}