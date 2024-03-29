pragma solidity >=0.7.0 <0.9.0;

import "./ex_math_lib.sol";

contract ERC20 {

    using SafeMath for uint256;
    mapping(address => uint256) balances;

    function transfer(address _to, uint256 _value) public returns (bool success) {
        balances[msg.sender] = balances[msg.sender].sub(_value);
        balances[_to] = balances[_to].add(_value);
        return true;
    }
}
