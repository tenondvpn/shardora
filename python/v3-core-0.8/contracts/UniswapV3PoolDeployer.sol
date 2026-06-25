// SPDX-License-Identifier: BUSL-1.1
pragma solidity ^0.8.20;

import './interfaces/IUniswapV3PoolDeployer.sol';
import './UniswapV3Pool.sol';

/// @title Uniswap V3 Pool Deployer
/// @notice Deploys Uniswap V3 pools using CREATE2 for deterministic addresses
/// @dev Upgraded to Solidity 0.8.20
contract UniswapV3PoolDeployer is IUniswapV3PoolDeployer {
    struct Parameters {
        address factory;
        address token0;
        address token1;
        uint24 fee;
        int24 tickSpacing;
    }

    /// @inheritdoc IUniswapV3PoolDeployer
    Parameters public override parameters;

    function deploy(
        address factory,
        address token0,
        address token1,
        uint24 fee,
        int24 tickSpacing
    ) internal returns (address pool) {
        parameters = Parameters({
            factory: factory,
            token0: token0,
            token1: token1,
            fee: fee,
            tickSpacing: tickSpacing
        });
        pool = address(
            new UniswapV3Pool{
                salt: keccak256(abi.encode(token0, token1, fee))
            }()
        );
        delete parameters;
    }
}
