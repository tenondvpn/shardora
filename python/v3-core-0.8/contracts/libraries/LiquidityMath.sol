// SPDX-License-Identifier: GPL-2.0-or-later
pragma solidity ^0.8.20;

/// @title Math library for liquidity
/// @dev Upgraded to Solidity 0.8.20 - uses built-in overflow checks
library LiquidityMath {
    /// @notice Add a signed liquidity delta to liquidity and revert if it overflows or underflows
    /// @param x The liquidity before change
    /// @param y The delta by which liquidity should be changed
    /// @return z The liquidity delta
    function addDelta(uint128 x, int128 y) internal pure returns (uint128 z) {
        if (y < 0) {
            z = x - uint128(-y);
            require(z < x, 'LS');
        } else {
            z = x + uint128(y);
            require(z >= x, 'LA');
        }
    }
}
