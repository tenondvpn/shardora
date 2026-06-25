// SPDX-License-Identifier: BUSL-1.1
pragma solidity ^0.8.20;

import './FullMath.sol';
import './FixedPoint128.sol';
import './LiquidityMath.sol';

/// @title Position
/// @notice Positions represent an owner address' liquidity between a lower and upper tick boundary
library Position {
    struct Info {
        uint128 liquidity;
        uint256 feeGrowthInside0LastX128;
        uint256 feeGrowthInside1LastX128;
        uint128 tokensOwed0;
        uint128 tokensOwed1;
    }

    function get(
        mapping(bytes32 => Info) storage self,
        address owner,
        int24 tickLower,
        int24 tickUpper
    ) internal view returns (Position.Info storage position) {
        position = self[keccak256(abi.encodePacked(owner, tickLower, tickUpper))];
    }

    function update(
        Info storage self,
        int128 liquidityDelta,
        uint256 feeGrowthInside0X128,
        uint256 feeGrowthInside1X128
    ) internal {
        Info memory _self = self;

        uint128 liquidityNext;
        if (liquidityDelta == 0) {
            require(_self.liquidity > 0, 'NP');
            liquidityNext = _self.liquidity;
        } else {
            liquidityNext = LiquidityMath.addDelta(_self.liquidity, liquidityDelta);
        }

        // 0.8.x: unchecked wrapping arithmetic is intentional here (fee growth can overflow)
        unchecked {
            uint128 tokensOwed0 = uint128(
                FullMath.mulDiv(feeGrowthInside0X128 - _self.feeGrowthInside0LastX128, _self.liquidity, FixedPoint128.Q128)
            );
            uint128 tokensOwed1 = uint128(
                FullMath.mulDiv(feeGrowthInside1X128 - _self.feeGrowthInside1LastX128, _self.liquidity, FixedPoint128.Q128)
            );

            if (liquidityDelta != 0) self.liquidity = liquidityNext;
            self.feeGrowthInside0LastX128 = feeGrowthInside0X128;
            self.feeGrowthInside1LastX128 = feeGrowthInside1X128;
            if (tokensOwed0 > 0 || tokensOwed1 > 0) {
                // overflow acceptable: must withdraw before hitting type(uint128).max
                self.tokensOwed0 += tokensOwed0;
                self.tokensOwed1 += tokensOwed1;
            }
        }
    }
}
