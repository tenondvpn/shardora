// SPDX-License-Identifier: BUSL-1.1
pragma solidity ^0.8.20;

import './interfaces/IUniswapV3Factory.sol';
import './UniswapV3PoolDeployer.sol';
import './NoDelegateCall.sol';
import './UniswapV3Pool.sol';

/// @title Canonical Uniswap V3 factory
/// @notice Deploys Uniswap V3 pools and manages ownership and control over pool protocol fees
/// @dev Upgraded to Solidity 0.8.20 - removed SafeMath, uses built-in overflow checks
contract UniswapV3Factory is IUniswapV3Factory, UniswapV3PoolDeployer, NoDelegateCall {
    /// @inheritdoc IUniswapV3Factory
    address public override owner;

    /// @inheritdoc IUniswapV3Factory
    mapping(uint24 => int24) public override feeAmountTickSpacing;
    
    /// @inheritdoc IUniswapV3Factory
    mapping(address => mapping(address => mapping(uint24 => address))) public override getPool;

    constructor() {
        owner = msg.sender;
        emit OwnerChanged(address(0), msg.sender);

        feeAmountTickSpacing[500] = 10;
        emit FeeAmountEnabled(500, 10);
        
        feeAmountTickSpacing[3000] = 60;
        emit FeeAmountEnabled(3000, 60);
        
        feeAmountTickSpacing[10000] = 200;
        emit FeeAmountEnabled(10000, 200);
    }

    /// @inheritdoc IUniswapV3Factory
    function createPool(
        address tokenA,
        address tokenB,
        uint24 fee
    ) external override noDelegateCall returns (address pool) {
        require(tokenA != tokenB, "Identical tokens");
        (address token0, address token1) = tokenA < tokenB 
            ? (tokenA, tokenB) 
            : (tokenB, tokenA);
        require(token0 != address(0), "Zero address");
        int24 tickSpacing = feeAmountTickSpacing[fee];
        require(tickSpacing != 0, "Fee not enabled");
        require(getPool[token0][token1][fee] == address(0), "Pool exists");
        pool = deploy(address(this), token0, token1, fee, tickSpacing);
        getPool[token0][token1][fee] = pool;
        getPool[token1][token0][fee] = pool;
        emit PoolCreated(token0, token1, fee, tickSpacing, pool);
    }

    // -----------------------------------------------------------------------
    // Debug helpers — only used when chain doesn't support in-contract CREATE2.
    // These do NOT change createPool logic. Remove after chain upgrade.
    // -----------------------------------------------------------------------

    /// @notice Write the parameters slot so an externally-deployed Pool can read them.
    ///         Call this, then deploy UniswapV3Pool from a script, then call registerPool().
    function setParameters(address tokenA, address tokenB, uint24 fee) external {
        require(msg.sender == owner, "Not owner");
        (address token0, address token1) = tokenA < tokenB ? (tokenA, tokenB) : (tokenB, tokenA);
        int24 tickSpacing = feeAmountTickSpacing[fee];
        require(tickSpacing != 0, "Fee not enabled");
        parameters = Parameters({
            factory: address(this),
            token0: token0,
            token1: token1,
            fee: fee,
            tickSpacing: tickSpacing
        });
    }

    /// @notice Register an externally-deployed pool address into the getPool mapping.
    ///         Must be called after setParameters + external Pool deployment.
    function registerPool(address tokenA, address tokenB, uint24 fee, address pool) external {
        require(msg.sender == owner, "Not owner");
        (address token0, address token1) = tokenA < tokenB ? (tokenA, tokenB) : (tokenB, tokenA);
        require(getPool[token0][token1][fee] == address(0), "Pool exists");
        require(pool != address(0), "Zero pool");
        delete parameters;
        getPool[token0][token1][fee] = pool;
        getPool[token1][token0][fee] = pool;
        int24 tickSpacing = feeAmountTickSpacing[fee];
        emit PoolCreated(token0, token1, fee, tickSpacing, pool);
    }

    /// @inheritdoc IUniswapV3Factory
    function setOwner(address _owner) external override {
        require(msg.sender == owner, "Not owner");
        emit OwnerChanged(owner, _owner);
        owner = _owner;
    }

    /// @inheritdoc IUniswapV3Factory
    function enableFeeAmount(uint24 fee, int24 tickSpacing) public override {
        require(msg.sender == owner, "Not owner");
        require(fee < 1000000, "Fee too large");
        require(tickSpacing > 0 && tickSpacing < 16384, "Invalid tick spacing");
        require(feeAmountTickSpacing[fee] == 0, "Fee already enabled");
        feeAmountTickSpacing[fee] = tickSpacing;
        emit FeeAmountEnabled(fee, tickSpacing);
    }
}
