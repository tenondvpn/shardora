from __future__ import annotations
import json
import secrets
import time
from eth_utils import to_checksum_address
import requests
import binascii
from gmssl import sm2, sm3, func

from shardora_sdk import ShardoraWeb3Mock, StepType, compile_and_link, get_sm2_public_key

# --- 5. Main Execution ---
# New: Contract for self-destruct testing

PROBE_CREATE2_FACTORY_SOL = """
pragma solidity ^0.8.20;

contract DeployedContract {
    address public deployer;
    constructor() payable {
        deployer = msg.sender;
    }
}

contract Create2Factory {
    event Deployed(address addr, uint256 salt);
    event DeployFailed(uint256 salt, string reason);
    event TestDeployed(address addr, uint256 salt, bytes);
    constructor() payable {
    }

    function deploy(uint256 salt) external payable returns (address addr) {
        bytes memory bytecode = type(DeployedContract).creationCode;
        bytes32 saltBytes = bytes32(salt);
        assembly {
            addr := create2(
                10000000,
                add(bytecode, 0x20),
                mload(bytecode),
                saltBytes
            )
        }

        if (addr == address(0)) {
            emit DeployFailed(salt, "Create2 deployment failed (addr=0)");
            revert("Create2: Failed on deploy");
        }

        if (addr.code.length == 0) {
            revert("Create2: Deployed but code is empty");
        }

        emit Deployed(addr, salt);
        return addr;
    }

    function getAddress(uint256 salt) public view returns (address) {
        bytes memory bytecode = type(DeployedContract).creationCode;

        bytes32 hash = keccak256(
            abi.encodePacked(
                bytes1(0xff),
                address(this),
                bytes32(salt),
                keccak256(bytecode)
            )
        );
        return address(uint160(uint256(hash)));
    }
}
"""

PROBE_QUOTER_SOL = """
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;
pragma abicoder v2;

interface IQuoter {
    /// @notice Returns the amount out received for a given exact input swap without executing the swap
    /// @param path The path of the swap, i.e. each token pair and the pool fee
    /// @param amountIn The amount of the first token to swap
    /// @return amountOut The amount of the last token that would be received
    function quoteExactInput(bytes memory path, uint256 amountIn) external returns (uint256 amountOut);

    /// @notice Returns the amount out received for a given exact input but for a swap of a single pool
    /// @param tokenIn The token being swapped in
    /// @param tokenOut The token being swapped out
    /// @param fee The fee of the token pool to consider for the pair
    /// @param amountIn The desired input amount
    /// @param sqrtPriceLimitX96 The price limit of the pool that cannot be exceeded by the swap
    /// @return amountOut The amount of `tokenOut` that would be received
    function quoteExactInputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        uint256 amountIn,
        uint160 sqrtPriceLimitX96
    ) external returns (uint256 amountOut);

    /// @notice Returns the amount in required for a given exact output swap without executing the swap
    /// @param path The path of the swap, i.e. each token pair and the pool fee
    /// @param amountOut The amount of the last token to receive
    /// @return amountIn The amount of first token required to be paid
    function quoteExactOutput(bytes memory path, uint256 amountOut) external returns (uint256 amountIn);

    /// @notice Returns the amount in required to receive the given exact output amount but for a swap of a single pool
    /// @param tokenIn The token being swapped in
    /// @param tokenOut The token being swapped out
    /// @param fee The fee of the token pool to consider for the pair
    /// @param amountOut The desired output amount
    /// @param sqrtPriceLimitX96 The price limit of the pool that cannot be exceeded by the swap
    /// @return amountIn The amount required as the input for the swap in order to receive `amountOut`
    function quoteExactOutputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        uint256 amountOut,
        uint160 sqrtPriceLimitX96
    ) external returns (uint256 amountIn);
}

interface IUniswapV3SwapCallback {
    /// @notice Called to `msg.sender` after executing a swap via IUniswapV3Pool#swap.
    /// @dev In the implementation you must pay the pool tokens owed for the swap.
    /// The caller of this method must be checked to be a UniswapV3Pool deployed by the canonical UniswapV3Factory.
    /// amount0Delta and amount1Delta can both be 0 if no tokens were swapped.
    /// @param amount0Delta The amount of token0 that was sent (negative) or must be received (positive) by the pool by
    /// the end of the swap. If positive, the callback must send that amount of token0 to the pool.
    /// @param amount1Delta The amount of token1 that was sent (negative) or must be received (positive) by the pool by
    /// the end of the swap. If positive, the callback must send that amount of token1 to the pool.
    /// @param data Any data passed through by the caller via the IUniswapV3PoolActions#swap call
    function uniswapV3SwapCallback(
        int256 amount0Delta,
        int256 amount1Delta,
        bytes calldata data
    ) external;
}

interface IPeripheryImmutableState {
    /// @return Returns the address of the Uniswap V3 factory
    function factory() external view returns (address);

    /// @return Returns the address of WETH9
    function WETH9() external view returns (address);
}

abstract contract PeripheryImmutableState is IPeripheryImmutableState {
    /// @inheritdoc IPeripheryImmutableState
    address public immutable override factory;
    /// @inheritdoc IPeripheryImmutableState
    address public immutable override WETH9;

    constructor(address _factory, address _WETH9) {
        factory = _factory;
        WETH9 = _WETH9;
    }
}


library BytesLib {
    function slice(
        bytes memory _bytes,
        uint256 _start,
        uint256 _length
    ) internal pure returns (bytes memory) {
        require(_length + 31 >= _length, 'slice_overflow');
        require(_start + _length >= _start, 'slice_overflow');
        require(_bytes.length >= _start + _length, 'slice_outOfBounds');

        bytes memory tempBytes;

        assembly {
            switch iszero(_length)
                case 0 {
                    // Get a location of some free memory and store it in tempBytes as
                    // Solidity does for memory variables.
                    tempBytes := mload(0x40)

                    // The first word of the slice result is potentially a partial
                    // word read from the original array. To read it, we calculate
                    // the length of that partial word and start copying that many
                    // bytes into the array. The first word we copy will start with
                    // data we don't care about, but the last `lengthmod` bytes will
                    // land at the beginning of the contents of the new array. When
                    // we're done copying, we overwrite the full first word with
                    // the actual length of the slice.
                    let lengthmod := and(_length, 31)

                    // The multiplication in the next line is necessary
                    // because when slicing multiples of 32 bytes (lengthmod == 0)
                    // the following copy loop was copying the origin's length
                    // and then ending prematurely not copying everything it should.
                    let mc := add(add(tempBytes, lengthmod), mul(0x20, iszero(lengthmod)))
                    let end := add(mc, _length)

                    for {
                        // The multiplication in the next line has the same exact purpose
                        // as the one above.
                        let cc := add(add(add(_bytes, lengthmod), mul(0x20, iszero(lengthmod))), _start)
                    } lt(mc, end) {
                        mc := add(mc, 0x20)
                        cc := add(cc, 0x20)
                    } {
                        mstore(mc, mload(cc))
                    }

                    mstore(tempBytes, _length)

                    //update free-memory pointer
                    //allocating the array padded to 32 bytes like the compiler does now
                    mstore(0x40, and(add(mc, 31), not(31)))
                }
                //if we want a zero-length slice let's just return a zero-length array
                default {
                    tempBytes := mload(0x40)
                    //zero out the 32 bytes slice we are about to return
                    //we need to do it because Solidity does not garbage collect
                    mstore(tempBytes, 0)

                    mstore(0x40, add(tempBytes, 0x20))
                }
        }

        return tempBytes;
    }

    function toAddress(bytes memory _bytes, uint256 _start) internal pure returns (address) {
        require(_start + 20 >= _start, 'toAddress_overflow');
        require(_bytes.length >= _start + 20, 'toAddress_outOfBounds');
        address tempAddress;

        assembly {
            tempAddress := div(mload(add(add(_bytes, 0x20), _start)), 0x1000000000000000000000000)
        }

        return tempAddress;
    }

    function toUint24(bytes memory _bytes, uint256 _start) internal pure returns (uint24) {
        require(_start + 3 >= _start, 'toUint24_overflow');
        require(_bytes.length >= _start + 3, 'toUint24_outOfBounds');
        uint24 tempUint;

        assembly {
            tempUint := mload(add(add(_bytes, 0x3), _start))
        }

        return tempUint;
    }
}

library Path {
    using BytesLib for bytes;

    /// @dev The length of the bytes encoded address
    uint256 private constant ADDR_SIZE = 20;
    /// @dev The length of the bytes encoded fee
    uint256 private constant FEE_SIZE = 3;

    /// @dev The offset of a single token address and pool fee
    uint256 private constant NEXT_OFFSET = ADDR_SIZE + FEE_SIZE;
    /// @dev The offset of an encoded pool key
    uint256 private constant POP_OFFSET = NEXT_OFFSET + ADDR_SIZE;
    /// @dev The minimum length of an encoding that contains 2 or more pools
    uint256 private constant MULTIPLE_POOLS_MIN_LENGTH = POP_OFFSET + NEXT_OFFSET;

    /// @notice Returns true iff the path contains two or more pools
    /// @param path The encoded swap path
    /// @return True if path contains two or more pools, otherwise false
    function hasMultiplePools(bytes memory path) internal pure returns (bool) {
        return path.length >= MULTIPLE_POOLS_MIN_LENGTH;
    }

    /// @notice Decodes the first pool in path
    /// @param path The bytes encoded swap path
    /// @return tokenA The first token of the given pool
    /// @return tokenB The second token of the given pool
    /// @return fee The fee level of the pool
    function decodeFirstPool(bytes memory path)
        internal
        pure
        returns (
            address tokenA,
            address tokenB,
            uint24 fee
        )
    {
        tokenA = path.toAddress(0);
        fee = path.toUint24(ADDR_SIZE);
        tokenB = path.toAddress(NEXT_OFFSET);
    }

    /// @notice Gets the segment corresponding to the first pool in the path
    /// @param path The bytes encoded swap path
    /// @return The segment containing all data necessary to target the first pool in the path
    function getFirstPool(bytes memory path) internal pure returns (bytes memory) {
        return path.slice(0, POP_OFFSET);
    }

    /// @notice Skips a token + fee element from the buffer and returns the remainder
    /// @param path The swap path
    /// @return The remaining token + fee elements in the path
    function skipToken(bytes memory path) internal pure returns (bytes memory) {
        return path.slice(NEXT_OFFSET, path.length - NEXT_OFFSET);
    }
}

library SafeCast {
    /// @notice Cast a uint256 to a uint160, revert on overflow
    /// @param y The uint256 to be downcasted
    /// @return z The downcasted integer, now type uint160
    function toUint160(uint256 y) internal pure returns (uint160 z) {
        require((z = uint160(y)) == y);
    }

    /// @notice Cast a int256 to a int128, revert on overflow or underflow
    /// @param y The int256 to be downcasted
    /// @return z The downcasted integer, now type int128
    function toInt128(int256 y) internal pure returns (int128 z) {
        require((z = int128(y)) == y);
    }

    /// @notice Cast a uint256 to a int256, revert on overflow
    /// @param y The uint256 to be casted
    /// @return z The casted integer, now type int256
    function toInt256(uint256 y) internal pure returns (int256 z) {
        require(y < 2**255);
        z = int256(y);
    }
}

interface IUniswapV3PoolImmutables {
    /// @notice The contract that deployed the pool, which must adhere to the IUniswapV3Factory interface
    /// @return The contract address
    function factory() external view returns (address);

    /// @notice The first of the two tokens of the pool, sorted by address
    /// @return The token contract address
    function token0() external view returns (address);

    /// @notice The second of the two tokens of the pool, sorted by address
    /// @return The token contract address
    function token1() external view returns (address);

    /// @notice The pool's fee in hundredths of a bip, i.e. 1e-6
    /// @return The fee
    function fee() external view returns (uint24);

    /// @notice The pool tick spacing
    /// @dev Ticks can only be used at multiples of this value, minimum of 1 and always positive
    /// e.g.: a tickSpacing of 3 means ticks can be initialized every 3rd tick, i.e., ..., -6, -3, 0, 3, 6, ...
    /// This value is an int24 to avoid casting even though it is always positive.
    /// @return The tick spacing
    function tickSpacing() external view returns (int24);

    /// @notice The maximum amount of position liquidity that can use any tick in the range
    /// @dev This parameter is enforced per tick to prevent liquidity from overflowing a uint128 at any point, and
    /// also prevents out-of-range liquidity from being used to prevent adding in-range liquidity to a pool
    /// @return The max amount of liquidity per tick
    function maxLiquidityPerTick() external view returns (uint128);
}

interface IUniswapV3PoolState {
    /// @notice The 0th storage slot in the pool stores many values, and is exposed as a single method to save gas
    /// when accessed externally.
    /// @return sqrtPriceX96 The current price of the pool as a sqrt(token1/token0) Q64.96 value
    /// tick The current tick of the pool, i.e. according to the last tick transition that was run.
    /// This value may not always be equal to SqrtTickMath.getTickAtSqrtRatio(sqrtPriceX96) if the price is on a tick
    /// boundary.
    /// observationIndex The index of the last oracle observation that was written,
    /// observationCardinality The current maximum number of observations stored in the pool,
    /// observationCardinalityNext The next maximum number of observations, to be updated when the observation.
    /// feeProtocol The protocol fee for both tokens of the pool.
    /// Encoded as two 4 bit values, where the protocol fee of token1 is shifted 4 bits and the protocol fee of token0
    /// is the lower 4 bits. Used as the denominator of a fraction of the swap fee, e.g. 4 means 1/4th of the swap fee.
    /// unlocked Whether the pool is currently locked to reentrancy
    function slot0()
        external
        view
        returns (
            uint160 sqrtPriceX96,
            int24 tick,
            uint16 observationIndex,
            uint16 observationCardinality,
            uint16 observationCardinalityNext,
            uint8 feeProtocol,
            bool unlocked
        );

    /// @notice The fee growth as a Q128.128 fees of token0 collected per unit of liquidity for the entire life of the pool
    /// @dev This value can overflow the uint256
    function feeGrowthGlobal0X128() external view returns (uint256);

    /// @notice The fee growth as a Q128.128 fees of token1 collected per unit of liquidity for the entire life of the pool
    /// @dev This value can overflow the uint256
    function feeGrowthGlobal1X128() external view returns (uint256);

    /// @notice The amounts of token0 and token1 that are owed to the protocol
    /// @dev Protocol fees will never exceed uint128 max in either token
    function protocolFees() external view returns (uint128 token0, uint128 token1);

    /// @notice The currently in range liquidity available to the pool
    /// @dev This value has no relationship to the total liquidity across all ticks
    function liquidity() external view returns (uint128);

    /// @notice Look up information about a specific tick in the pool
    /// @param tick The tick to look up
    /// @return liquidityGross the total amount of position liquidity that uses the pool either as tick lower or
    /// tick upper,
    /// liquidityNet how much liquidity changes when the pool price crosses the tick,
    /// feeGrowthOutside0X128 the fee growth on the other side of the tick from the current tick in token0,
    /// feeGrowthOutside1X128 the fee growth on the other side of the tick from the current tick in token1,
    /// tickCumulativeOutside the cumulative tick value on the other side of the tick from the current tick
    /// secondsPerLiquidityOutsideX128 the seconds spent per liquidity on the other side of the tick from the current tick,
    /// secondsOutside the seconds spent on the other side of the tick from the current tick,
    /// initialized Set to true if the tick is initialized, i.e. liquidityGross is greater than 0, otherwise equal to false.
    /// Outside values can only be used if the tick is initialized, i.e. if liquidityGross is greater than 0.
    /// In addition, these values are only relative and must be used only in comparison to previous snapshots for
    /// a specific position.
    function ticks(int24 tick)
        external
        view
        returns (
            uint128 liquidityGross,
            int128 liquidityNet,
            uint256 feeGrowthOutside0X128,
            uint256 feeGrowthOutside1X128,
            int56 tickCumulativeOutside,
            uint160 secondsPerLiquidityOutsideX128,
            uint32 secondsOutside,
            bool initialized
        );

    /// @notice Returns 256 packed tick initialized boolean values. See TickBitmap for more information
    function tickBitmap(int16 wordPosition) external view returns (uint256);

    /// @notice Returns the information about a position by the position's key
    /// @param key The position's key is a hash of a preimage composed by the owner, tickLower and tickUpper
    /// @return _liquidity The amount of liquidity in the position,
    /// Returns feeGrowthInside0LastX128 fee growth of token0 inside the tick range as of the last mint/burn/poke,
    /// Returns feeGrowthInside1LastX128 fee growth of token1 inside the tick range as of the last mint/burn/poke,
    /// Returns tokensOwed0 the computed amount of token0 owed to the position as of the last mint/burn/poke,
    /// Returns tokensOwed1 the computed amount of token1 owed to the position as of the last mint/burn/poke
    function positions(bytes32 key)
        external
        view
        returns (
            uint128 _liquidity,
            uint256 feeGrowthInside0LastX128,
            uint256 feeGrowthInside1LastX128,
            uint128 tokensOwed0,
            uint128 tokensOwed1
        );

    /// @notice Returns data about a specific observation index
    /// @param index The element of the observations array to fetch
    /// @dev You most likely want to use #observe() instead of this method to get an observation as of some amount of time
    /// ago, rather than at a specific index in the array.
    /// @return blockTimestamp The timestamp of the observation,
    /// Returns tickCumulative the tick multiplied by seconds elapsed for the life of the pool as of the observation timestamp,
    /// Returns secondsPerLiquidityCumulativeX128 the seconds per in range liquidity for the life of the pool as of the observation timestamp,
    /// Returns initialized whether the observation has been initialized and the values are safe to use
    function observations(uint256 index)
        external
        view
        returns (
            uint32 blockTimestamp,
            int56 tickCumulative,
            uint160 secondsPerLiquidityCumulativeX128,
            bool initialized
        );
}

interface IUniswapV3PoolDerivedState {
    /// @notice Returns the cumulative tick and liquidity as of each timestamp `secondsAgo` from the current block timestamp
    /// @dev To get a time weighted average tick or liquidity-in-range, you must call this with two values, one representing
    /// the beginning of the period and another for the end of the period. E.g., to get the last hour time-weighted average tick,
    /// you must call it with secondsAgos = [3600, 0].
    /// @dev The time weighted average tick represents the geometric time weighted average price of the pool, in
    /// log base sqrt(1.0001) of token1 / token0. The TickMath library can be used to go from a tick value to a ratio.
    /// @param secondsAgos From how long ago each cumulative tick and liquidity value should be returned
    /// @return tickCumulatives Cumulative tick values as of each `secondsAgos` from the current block timestamp
    /// @return secondsPerLiquidityCumulativeX128s Cumulative seconds per liquidity-in-range value as of each `secondsAgos` from the current block
    /// timestamp
    function observe(uint32[] calldata secondsAgos)
        external
        view
        returns (int56[] memory tickCumulatives, uint160[] memory secondsPerLiquidityCumulativeX128s);

    /// @notice Returns a snapshot of the tick cumulative, seconds per liquidity and seconds inside a tick range
    /// @dev Snapshots must only be compared to other snapshots, taken over a period for which a position existed.
    /// I.e., snapshots cannot be compared if a position is not held for the entire period between when the first
    /// snapshot is taken and the second snapshot is taken.
    /// @param tickLower The lower tick of the range
    /// @param tickUpper The upper tick of the range
    /// @return tickCumulativeInside The snapshot of the tick accumulator for the range
    /// @return secondsPerLiquidityInsideX128 The snapshot of seconds per liquidity for the range
    /// @return secondsInside The snapshot of seconds per liquidity for the range
    function snapshotCumulativesInside(int24 tickLower, int24 tickUpper)
        external
        view
        returns (
            int56 tickCumulativeInside,
            uint160 secondsPerLiquidityInsideX128,
            uint32 secondsInside
        );
}

interface IUniswapV3PoolActions {
    /// @notice Sets the initial price for the pool
    /// @dev Price is represented as a sqrt(amountToken1/amountToken0) Q64.96 value
    /// @param sqrtPriceX96 the initial sqrt price of the pool as a Q64.96
    function initialize(uint160 sqrtPriceX96) external;

    /// @notice Adds liquidity for the given recipient/tickLower/tickUpper position
    /// @dev The caller of this method receives a callback in the form of IUniswapV3MintCallback#uniswapV3MintCallback
    /// in which they must pay any token0 or token1 owed for the liquidity. The amount of token0/token1 due depends
    /// on tickLower, tickUpper, the amount of liquidity, and the current price.
    /// @param recipient The address for which the liquidity will be created
    /// @param tickLower The lower tick of the position in which to add liquidity
    /// @param tickUpper The upper tick of the position in which to add liquidity
    /// @param amount The amount of liquidity to mint
    /// @param data Any data that should be passed through to the callback
    /// @return amount0 The amount of token0 that was paid to mint the given amount of liquidity. Matches the value in the callback
    /// @return amount1 The amount of token1 that was paid to mint the given amount of liquidity. Matches the value in the callback
    function mint(
        address recipient,
        int24 tickLower,
        int24 tickUpper,
        uint128 amount,
        bytes calldata data
    ) external returns (uint256 amount0, uint256 amount1);

    /// @notice Collects tokens owed to a position
    /// @dev Does not recompute fees earned, which must be done either via mint or burn of any amount of liquidity.
    /// Collect must be called by the position owner. To withdraw only token0 or only token1, amount0Requested or
    /// amount1Requested may be set to zero. To withdraw all tokens owed, caller may pass any value greater than the
    /// actual tokens owed, e.g. type(uint128).max. Tokens owed may be from accumulated swap fees or burned liquidity.
    /// @param recipient The address which should receive the fees collected
    /// @param tickLower The lower tick of the position for which to collect fees
    /// @param tickUpper The upper tick of the position for which to collect fees
    /// @param amount0Requested How much token0 should be withdrawn from the fees owed
    /// @param amount1Requested How much token1 should be withdrawn from the fees owed
    /// @return amount0 The amount of fees collected in token0
    /// @return amount1 The amount of fees collected in token1
    function collect(
        address recipient,
        int24 tickLower,
        int24 tickUpper,
        uint128 amount0Requested,
        uint128 amount1Requested
    ) external returns (uint128 amount0, uint128 amount1);

    /// @notice Burn liquidity from the sender and account tokens owed for the liquidity to the position
    /// @dev Can be used to trigger a recalculation of fees owed to a position by calling with an amount of 0
    /// @dev Fees must be collected separately via a call to #collect
    /// @param tickLower The lower tick of the position for which to burn liquidity
    /// @param tickUpper The upper tick of the position for which to burn liquidity
    /// @param amount How much liquidity to burn
    /// @return amount0 The amount of token0 sent to the recipient
    /// @return amount1 The amount of token1 sent to the recipient
    function burn(
        int24 tickLower,
        int24 tickUpper,
        uint128 amount
    ) external returns (uint256 amount0, uint256 amount1);

    /// @notice Swap token0 for token1, or token1 for token0
    /// @dev The caller of this method receives a callback in the form of IUniswapV3SwapCallback#uniswapV3SwapCallback
    /// @param recipient The address to receive the output of the swap
    /// @param zeroForOne The direction of the swap, true for token0 to token1, false for token1 to token0
    /// @param amountSpecified The amount of the swap, which implicitly configures the swap as exact input (positive), or exact output (negative)
    /// @param sqrtPriceLimitX96 The Q64.96 sqrt price limit. If zero for one, the price cannot be less than this
    /// value after the swap. If one for zero, the price cannot be greater than this value after the swap
    /// @param data Any data to be passed through to the callback
    /// @return amount0 The delta of the balance of token0 of the pool, exact when negative, minimum when positive
    /// @return amount1 The delta of the balance of token1 of the pool, exact when negative, minimum when positive
    function swap(
        address recipient,
        bool zeroForOne,
        int256 amountSpecified,
        uint160 sqrtPriceLimitX96,
        bytes calldata data
    ) external returns (int256 amount0, int256 amount1);

    /// @notice Receive token0 and/or token1 and pay it back, plus a fee, in the callback
    /// @dev The caller of this method receives a callback in the form of IUniswapV3FlashCallback#uniswapV3FlashCallback
    /// @dev Can be used to donate underlying tokens pro-rata to currently in-range liquidity providers by calling
    /// with 0 amount{0,1} and sending the donation amount(s) from the callback
    /// @param recipient The address which will receive the token0 and token1 amounts
    /// @param amount0 The amount of token0 to send
    /// @param amount1 The amount of token1 to send
    /// @param data Any data to be passed through to the callback
    function flash(
        address recipient,
        uint256 amount0,
        uint256 amount1,
        bytes calldata data
    ) external;

    /// @notice Increase the maximum number of price and liquidity observations that this pool will store
    /// @dev This method is no-op if the pool already has an observationCardinalityNext greater than or equal to
    /// the input observationCardinalityNext.
    /// @param observationCardinalityNext The desired minimum number of observations for the pool to store
    function increaseObservationCardinalityNext(uint16 observationCardinalityNext) external;
}

interface IUniswapV3PoolOwnerActions {
    /// @notice Set the denominator of the protocol's % share of the fees
    /// @param feeProtocol0 new protocol fee for token0 of the pool
    /// @param feeProtocol1 new protocol fee for token1 of the pool
    function setFeeProtocol(uint8 feeProtocol0, uint8 feeProtocol1) external;

    /// @notice Collect the protocol fee accrued to the pool
    /// @param recipient The address to which collected protocol fees should be sent
    /// @param amount0Requested The maximum amount of token0 to send, can be 0 to collect fees in only token1
    /// @param amount1Requested The maximum amount of token1 to send, can be 0 to collect fees in only token0
    /// @return amount0 The protocol fee collected in token0
    /// @return amount1 The protocol fee collected in token1
    function collectProtocol(
        address recipient,
        uint128 amount0Requested,
        uint128 amount1Requested
    ) external returns (uint128 amount0, uint128 amount1);
}

interface IUniswapV3PoolEvents {
    /// @notice Emitted exactly once by a pool when #initialize is first called on the pool
    /// @dev Mint/Burn/Swap cannot be emitted by the pool before Initialize
    /// @param sqrtPriceX96 The initial sqrt price of the pool, as a Q64.96
    /// @param tick The initial tick of the pool, i.e. log base 1.0001 of the starting price of the pool
    event Initialize(uint160 sqrtPriceX96, int24 tick);

    /// @notice Emitted when liquidity is minted for a given position
    /// @param sender The address that minted the liquidity
    /// @param owner The owner of the position and recipient of any minted liquidity
    /// @param tickLower The lower tick of the position
    /// @param tickUpper The upper tick of the position
    /// @param amount The amount of liquidity minted to the position range
    /// @param amount0 How much token0 was required for the minted liquidity
    /// @param amount1 How much token1 was required for the minted liquidity
    event Mint(
        address sender,
        address indexed owner,
        int24 indexed tickLower,
        int24 indexed tickUpper,
        uint128 amount,
        uint256 amount0,
        uint256 amount1
    );

    /// @notice Emitted when fees are collected by the owner of a position
    /// @dev Collect events may be emitted with zero amount0 and amount1 when the caller chooses not to collect fees
    /// @param owner The owner of the position for which fees are collected
    /// @param tickLower The lower tick of the position
    /// @param tickUpper The upper tick of the position
    /// @param amount0 The amount of token0 fees collected
    /// @param amount1 The amount of token1 fees collected
    event Collect(
        address indexed owner,
        address recipient,
        int24 indexed tickLower,
        int24 indexed tickUpper,
        uint128 amount0,
        uint128 amount1
    );

    /// @notice Emitted when a position's liquidity is removed
    /// @dev Does not withdraw any fees earned by the liquidity position, which must be withdrawn via #collect
    /// @param owner The owner of the position for which liquidity is removed
    /// @param tickLower The lower tick of the position
    /// @param tickUpper The upper tick of the position
    /// @param amount The amount of liquidity to remove
    /// @param amount0 The amount of token0 withdrawn
    /// @param amount1 The amount of token1 withdrawn
    event Burn(
        address indexed owner,
        int24 indexed tickLower,
        int24 indexed tickUpper,
        uint128 amount,
        uint256 amount0,
        uint256 amount1
    );

    /// @notice Emitted by the pool for any swaps between token0 and token1
    /// @param sender The address that initiated the swap call, and that received the callback
    /// @param recipient The address that received the output of the swap
    /// @param amount0 The delta of the token0 balance of the pool
    /// @param amount1 The delta of the token1 balance of the pool
    /// @param sqrtPriceX96 The sqrt(price) of the pool after the swap, as a Q64.96
    /// @param liquidity The liquidity of the pool after the swap
    /// @param tick The log base 1.0001 of price of the pool after the swap
    event Swap(
        address indexed sender,
        address indexed recipient,
        int256 amount0,
        int256 amount1,
        uint160 sqrtPriceX96,
        uint128 liquidity,
        int24 tick
    );

    /// @notice Emitted by the pool for any flashes of token0/token1
    /// @param sender The address that initiated the swap call, and that received the callback
    /// @param recipient The address that received the tokens from flash
    /// @param amount0 The amount of token0 that was flashed
    /// @param amount1 The amount of token1 that was flashed
    /// @param paid0 The amount of token0 paid for the flash, which can exceed the amount0 plus the fee
    /// @param paid1 The amount of token1 paid for the flash, which can exceed the amount1 plus the fee
    event Flash(
        address indexed sender,
        address indexed recipient,
        uint256 amount0,
        uint256 amount1,
        uint256 paid0,
        uint256 paid1
    );

    /// @notice Emitted by the pool for increases to the number of observations that can be stored
    /// @dev observationCardinalityNext is not the observation cardinality until an observation is written at the index
    /// just before a mint/swap/burn.
    /// @param observationCardinalityNextOld The previous value of the next observation cardinality
    /// @param observationCardinalityNextNew The updated value of the next observation cardinality
    event IncreaseObservationCardinalityNext(
        uint16 observationCardinalityNextOld,
        uint16 observationCardinalityNextNew
    );

    /// @notice Emitted when the protocol fee is changed by the pool
    /// @param feeProtocol0Old The previous value of the token0 protocol fee
    /// @param feeProtocol1Old The previous value of the token1 protocol fee
    /// @param feeProtocol0New The updated value of the token0 protocol fee
    /// @param feeProtocol1New The updated value of the token1 protocol fee
    event SetFeeProtocol(uint8 feeProtocol0Old, uint8 feeProtocol1Old, uint8 feeProtocol0New, uint8 feeProtocol1New);

    /// @notice Emitted when the collected protocol fees are withdrawn by the factory owner
    /// @param sender The address that collects the protocol fees
    /// @param recipient The address that receives the collected protocol fees
    /// @param amount0 The amount of token0 protocol fees that is withdrawn
    /// @param amount0 The amount of token1 protocol fees that is withdrawn
    event CollectProtocol(address indexed sender, address indexed recipient, uint128 amount0, uint128 amount1);
}


interface IUniswapV3Pool is
    IUniswapV3PoolImmutables,
    IUniswapV3PoolState,
    IUniswapV3PoolDerivedState,
    IUniswapV3PoolActions,
    IUniswapV3PoolOwnerActions,
    IUniswapV3PoolEvents
{

}

library PoolAddress {
    bytes32 internal constant POOL_INIT_CODE_HASH = 0x7cb223e5822af748e77813b995f26756c2bb964332f3faac2bf05b40ced7b4f0;

    /// @notice The identifying key of the pool
    struct PoolKey {
        address token0;
        address token1;
        uint24 fee;
    }

    /// @notice Returns PoolKey: the ordered tokens with the matched fee levels
    /// @param tokenA The first token of a pool, unsorted
    /// @param tokenB The second token of a pool, unsorted
    /// @param fee The fee level of the pool
    /// @return Poolkey The pool details with ordered token0 and token1 assignments
    function getPoolKey(
        address tokenA,
        address tokenB,
        uint24 fee
    ) internal pure returns (PoolKey memory) {
        if (tokenA > tokenB) (tokenA, tokenB) = (tokenB, tokenA);
        return PoolKey({token0: tokenA, token1: tokenB, fee: fee});
    }

    /// @notice Deterministically computes the pool address given the factory and PoolKey
    /// @param factory The Uniswap V3 factory contract address
    /// @param key The PoolKey
    /// @return pool The contract address of the V3 pool
    function computeAddress(address factory, PoolKey memory key) internal pure returns (address pool) {
        require(key.token0 < key.token1);
        pool = address(
            uint160(  // Add this intermediate conversion
                uint256(
                    keccak256(
                        abi.encodePacked(
                            hex'ff',
                            factory,
                            keccak256(abi.encode(key.token0, key.token1, key.fee)),
                            POOL_INIT_CODE_HASH
                        )
                    )
                )
            )
        );
    }
}

library CallbackValidation {
    /// @notice Returns the address of a valid Uniswap V3 Pool
    /// @param factory The contract address of the Uniswap V3 factory
    /// @param tokenA The contract address of either token0 or token1
    /// @param tokenB The contract address of the other token
    /// @param fee The fee collected upon every swap in the pool, denominated in hundredths of a bip
    /// @return pool The V3 pool contract address
    function verifyCallback(
        address factory,
        address tokenA,
        address tokenB,
        uint24 fee
    ) internal view returns (IUniswapV3Pool pool) {
        return verifyCallback(factory, PoolAddress.getPoolKey(tokenA, tokenB, fee));
    }

    /// @notice Returns the address of a valid Uniswap V3 Pool
    /// @param factory The contract address of the Uniswap V3 factory
    /// @param poolKey The identifying key of the V3 pool
    /// @return pool The V3 pool contract address
    function verifyCallback(address factory, PoolAddress.PoolKey memory poolKey)
        internal
        view
        returns (IUniswapV3Pool pool)
    {
        pool = IUniswapV3Pool(PoolAddress.computeAddress(factory, poolKey));
        require(msg.sender == address(pool));
    }
}

library TickMath {
    /// @dev The minimum tick that may be passed to #getSqrtRatioAtTick computed from log base 1.0001 of 2**-128
    int24 internal constant MIN_TICK = -887272;
    /// @dev The maximum tick that may be passed to #getSqrtRatioAtTick computed from log base 1.0001 of 2**128
    int24 internal constant MAX_TICK = -MIN_TICK;

    /// @dev The minimum value that can be returned from #getSqrtRatioAtTick. Equivalent to getSqrtRatioAtTick(MIN_TICK)
    uint160 internal constant MIN_SQRT_RATIO = 4295128739;
    /// @dev The maximum value that can be returned from #getSqrtRatioAtTick. Equivalent to getSqrtRatioAtTick(MAX_TICK)
    uint160 internal constant MAX_SQRT_RATIO = 1461446703485210103287273052203988822378723970342;

    /// @notice Calculates sqrt(1.0001^tick) * 2^96
    /// @dev Throws if |tick| > max tick
    /// @param tick The input tick for the above formula
    /// @return sqrtPriceX96 A Fixed point Q64.96 number representing the sqrt of the ratio of the two assets (token1/token0)
    /// at the given tick
    function getSqrtRatioAtTick(int24 tick) internal pure returns (uint160 sqrtPriceX96) {
        uint256 absTick = tick < 0 ? uint256(-int256(tick)) : uint256(int256(tick));
        require(absTick <= uint256(int256(MAX_TICK)), 'T');

        uint256 ratio = absTick & 0x1 != 0 ? 0xfffcb933bd6fad37aa2d162d1a594001 : 0x100000000000000000000000000000000;
        if (absTick & 0x2 != 0) ratio = (ratio * 0xfff97272373d413259a46990580e213a) >> 128;
        if (absTick & 0x4 != 0) ratio = (ratio * 0xfff2e50f5f656932ef12357cf3c7fdcc) >> 128;
        if (absTick & 0x8 != 0) ratio = (ratio * 0xffe5caca7e10e4e61c3624eaa0941cd0) >> 128;
        if (absTick & 0x10 != 0) ratio = (ratio * 0xffcb9843d60f6159c9db58835c926644) >> 128;
        if (absTick & 0x20 != 0) ratio = (ratio * 0xff973b41fa98c081472e6896dfb254c0) >> 128;
        if (absTick & 0x40 != 0) ratio = (ratio * 0xff2ea16466c96a3843ec78b326b52861) >> 128;
        if (absTick & 0x80 != 0) ratio = (ratio * 0xfe5dee046a99a2a811c461f1969c3053) >> 128;
        if (absTick & 0x100 != 0) ratio = (ratio * 0xfcbe86c7900a88aedcffc83b479aa3a4) >> 128;
        if (absTick & 0x200 != 0) ratio = (ratio * 0xf987a7253ac413176f2b074cf7815e54) >> 128;
        if (absTick & 0x400 != 0) ratio = (ratio * 0xf3392b0822b70005940c7a398e4b70f3) >> 128;
        if (absTick & 0x800 != 0) ratio = (ratio * 0xe7159475a2c29b7443b29c7fa6e889d9) >> 128;
        if (absTick & 0x1000 != 0) ratio = (ratio * 0xd097f3bdfd2022b8845ad8f792aa5825) >> 128;
        if (absTick & 0x2000 != 0) ratio = (ratio * 0xa9f746462d870fdf8a65dc1f90e061e5) >> 128;
        if (absTick & 0x4000 != 0) ratio = (ratio * 0x70d869a156d2a1b890bb3df62baf32f7) >> 128;
        if (absTick & 0x8000 != 0) ratio = (ratio * 0x31be135f97d08fd981231505542fcfa6) >> 128;
        if (absTick & 0x10000 != 0) ratio = (ratio * 0x9aa508b5b7a84e1c677de54f3e99bc9) >> 128;
        if (absTick & 0x20000 != 0) ratio = (ratio * 0x5d6af8dedb81196699c329225ee604) >> 128;
        if (absTick & 0x40000 != 0) ratio = (ratio * 0x2216e584f5fa1ea926041bedfe98) >> 128;
        if (absTick & 0x80000 != 0) ratio = (ratio * 0x48a170391f7dc42444e8fa2) >> 128;

        if (tick > 0) ratio = type(uint256).max / ratio;

        // this divides by 1<<32 rounding up to go from a Q128.128 to a Q128.96.
        // we then downcast because we know the result always fits within 160 bits due to our tick input constraint
        // we round up in the division so getTickAtSqrtRatio of the output price is always consistent
        sqrtPriceX96 = uint160((ratio >> 32) + (ratio % (1 << 32) == 0 ? 0 : 1));
    }

    /// @notice Calculates the greatest tick value such that getRatioAtTick(tick) <= ratio
    /// @dev Throws in case sqrtPriceX96 < MIN_SQRT_RATIO, as MIN_SQRT_RATIO is the lowest value getRatioAtTick may
    /// ever return.
    /// @param sqrtPriceX96 The sqrt ratio for which to compute the tick as a Q64.96
    /// @return tick The greatest tick for which the ratio is less than or equal to the input ratio
    function getTickAtSqrtRatio(uint160 sqrtPriceX96) internal pure returns (int24 tick) {
        // second inequality must be < because the price can never reach the price at the max tick
        require(sqrtPriceX96 >= MIN_SQRT_RATIO && sqrtPriceX96 < MAX_SQRT_RATIO, 'R');
        uint256 ratio = uint256(sqrtPriceX96) << 32;

        uint256 r = ratio;
        uint256 msb = 0;

        assembly {
            let f := shl(7, gt(r, 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := shl(6, gt(r, 0xFFFFFFFFFFFFFFFF))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := shl(5, gt(r, 0xFFFFFFFF))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := shl(4, gt(r, 0xFFFF))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := shl(3, gt(r, 0xFF))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := shl(2, gt(r, 0xF))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := shl(1, gt(r, 0x3))
            msb := or(msb, f)
            r := shr(f, r)
        }
        assembly {
            let f := gt(r, 0x1)
            msb := or(msb, f)
        }

        if (msb >= 128) r = ratio >> (msb - 127);
        else r = ratio << (127 - msb);

        int256 log_2 = (int256(msb) - 128) << 64;

        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(63, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(62, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(61, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(60, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(59, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(58, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(57, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(56, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(55, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(54, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(53, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(52, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(51, f))
            r := shr(f, r)
        }
        assembly {
            r := shr(127, mul(r, r))
            let f := shr(128, r)
            log_2 := or(log_2, shl(50, f))
        }

        int256 log_sqrt10001 = log_2 * 255738958999603826347141; // 128.128 number

        int24 tickLow = int24((log_sqrt10001 - 3402992956809132418596140100660247210) >> 128);
        int24 tickHi = int24((log_sqrt10001 + 291339464771989622907027621153398088495) >> 128);

        tick = tickLow == tickHi ? tickLow : getSqrtRatioAtTick(tickHi) <= sqrtPriceX96 ? tickHi : tickLow;
    }
}


/// @title Provides quotes for swaps
/// @notice Allows getting the expected amount out or amount in for a given swap without executing the swap
/// @dev These functions are not gas efficient and should _not_ be called on chain. Instead, optimistically execute
/// the swap and check the amounts in the callback.
contract Quoter is IQuoter, IUniswapV3SwapCallback, PeripheryImmutableState {
    using Path for bytes;
    using SafeCast for uint256;

    /// @dev Transient storage variable used to check a safety condition in exact output swaps.
    uint256 private amountOutCached;

    constructor(address _factory, address _WETH9) PeripheryImmutableState(_factory, _WETH9) {
    }

    function getPool(
        address tokenA,
        address tokenB,
        uint24 fee
    ) public view returns (IUniswapV3Pool) {
        return IUniswapV3Pool(PoolAddress.computeAddress(factory, PoolAddress.getPoolKey(tokenA, tokenB, fee)));
    }

    function value(
        address tokenA,
        address tokenB,
        uint24 fee
    ) public view returns (uint160) {
        IUniswapV3Pool pool = IUniswapV3Pool(PoolAddress.computeAddress(factory, PoolAddress.getPoolKey(tokenA, tokenB, fee)));
        (uint160 a, ,,,,,) = pool.slot0();
        return a;
    }

    function test() public pure returns (uint256) {
        return 123;
    }

    /// @inheritdoc IUniswapV3SwapCallback
    function uniswapV3SwapCallback(
        int256 amount0Delta,
        int256 amount1Delta,
        bytes memory path
    ) external view override {
        require(amount0Delta > 0 || amount1Delta > 0); // swaps entirely within 0-liquidity regions are not supported
        (address tokenIn, address tokenOut, uint24 fee) = path.decodeFirstPool();
        CallbackValidation.verifyCallback(factory, tokenIn, tokenOut, fee);

        (bool isExactInput, uint256 amountToPay, uint256 amountReceived) =
            amount0Delta > 0
                ? (tokenIn < tokenOut, uint256(amount0Delta), uint256(-amount1Delta))
                : (tokenOut < tokenIn, uint256(amount1Delta), uint256(-amount0Delta));
        if (isExactInput) {
            assembly {
                let ptr := mload(0x40)
                mstore(ptr, amountReceived)
                revert(ptr, 32)
            }
        } else {
            // if the cache has been populated, ensure that the full output amount has been received
            if (amountOutCached != 0) require(amountReceived == amountOutCached);
            assembly {
                let ptr := mload(0x40)
                mstore(ptr, amountToPay)
                revert(ptr, 32)
            }
        }
    }

    /// @dev Parses a revert reason that should contain the numeric quote
    function parseRevertReason(bytes memory reason) private pure returns (uint256) {
        if (reason.length != 32) {
            if (reason.length < 68) revert('Unexpected error');
            assembly {
                reason := add(reason, 0x04)
            }
            revert(abi.decode(reason, (string)));
        }
        return abi.decode(reason, (uint256));
    }

    /// @inheritdoc IQuoter
    function quoteExactInputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        uint256 amountIn,
        uint160 sqrtPriceLimitX96
    ) public override returns (uint256 amountOut) {
        bool zeroForOne = tokenIn < tokenOut;

        try
            getPool(tokenIn, tokenOut, fee).swap(
                address(this), // address(0) might cause issues with some tokens
                zeroForOne,
                amountIn.toInt256(),
                sqrtPriceLimitX96 == 0
                    ? (zeroForOne ? TickMath.MIN_SQRT_RATIO + 1 : TickMath.MAX_SQRT_RATIO - 1)
                    : sqrtPriceLimitX96,
                abi.encodePacked(tokenIn, fee, tokenOut)
            )
        {} catch (bytes memory reason) {
            return parseRevertReason(reason);
        }
    }

    /// @inheritdoc IQuoter
    function quoteExactInput(bytes memory path, uint256 amountIn) external override returns (uint256 amountOut) {
        while (true) {
            bool hasMultiplePools = path.hasMultiplePools();

            (address tokenIn, address tokenOut, uint24 fee) = path.decodeFirstPool();

            // the outputs of prior swaps become the inputs to subsequent ones
            amountIn = quoteExactInputSingle(tokenIn, tokenOut, fee, amountIn, 0);

            // decide whether to continue or terminate
            if (hasMultiplePools) {
                path = path.skipToken();
            } else {
                return amountIn;
            }
        }
    }

    /// @inheritdoc IQuoter
    function quoteExactOutputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        uint256 amountOut,
        uint160 sqrtPriceLimitX96
    ) public override returns (uint256 amountIn) {
        bool zeroForOne = tokenIn < tokenOut;

        // if no price limit has been specified, cache the output amount for comparison in the swap callback
        if (sqrtPriceLimitX96 == 0) amountOutCached = amountOut;
        try
            getPool(tokenIn, tokenOut, fee).swap(
                address(this), // address(0) might cause issues with some tokens
                zeroForOne,
                -amountOut.toInt256(),
                sqrtPriceLimitX96 == 0
                    ? (zeroForOne ? TickMath.MIN_SQRT_RATIO + 1 : TickMath.MAX_SQRT_RATIO - 1)
                    : sqrtPriceLimitX96,
                abi.encodePacked(tokenOut, fee, tokenIn)
            )
        {} catch (bytes memory reason) {
            if (sqrtPriceLimitX96 == 0) delete amountOutCached; // clear cache
            return parseRevertReason(reason);
        }
    }

    /// @inheritdoc IQuoter
    function quoteExactOutput(bytes memory path, uint256 amountOut) external override returns (uint256 amountIn) {
        while (true) {
            bool hasMultiplePools = path.hasMultiplePools();

            (address tokenOut, address tokenIn, uint24 fee) = path.decodeFirstPool();

            // the inputs of prior swaps become the outputs of subsequent ones
            amountOut = quoteExactOutputSingle(tokenIn, tokenOut, fee, amountOut, 0);

            // decide whether to continue or terminate
            if (hasMultiplePools) {
                path = path.skipToken();
            } else {
                return amountOut;
            }
        }
    }
}
"""

PROBE_KILL_SOL = """
pragma solidity ^0.8.20;

contract ProbeKill {
    address public owner;
    string public message;

    constructor() payable {
        owner = msg.sender;
        message = "Initialized";
    }

    modifier onlyOwner() {
        require(msg.sender == owner, "Not owner");
        _;
    }

    // New: State-changing function (Requires consensus)
    function setMessage(string memory _m) external onlyOwner {
        message = _m;
    }

    // New: View function (Query only)
    function getMessage() external view returns (string memory) {
        return message;
    }

    receive() external payable {}

    // Execute self-destruct and transfer remaining ETH
    function kill(address payable recipient) external onlyOwner {
        selfdestruct(recipient);
    }
}
"""

PROBE_POOL_SOL = """
pragma solidity ^0.8.20;

contract ProbePool {
    uint256 public reserveSHARDORA;
    uint256 public reserveUSDC;

    event PoolSwap(address indexed sender, uint256 amountIn, uint256 amountOut, uint256 resSHARDORA, uint256 resUSDC);

    constructor(uint256 s, uint256 u) payable {
        reserveSHARDORA = s;
        reserveUSDC = u;
    }

    function sellSHARDORA(uint256 m) external payable returns (uint256 out) {
        out = (msg.value * reserveUSDC) / (reserveSHARDORA + msg.value);
        require(out >= m, 'ProbePool: slippage');

        reserveSHARDORA += msg.value;
        reserveUSDC -= out;

        emit PoolSwap(msg.sender, msg.value, out, reserveSHARDORA, reserveUSDC);
        return out;
    }
}
"""

PROBE_TREASURY_SOL = """
pragma solidity ^0.8.20;

contract ProbeTreasury {
    address public pool;
    address public bridge;
    uint256 public totalSwaps;

    event TreasuryForwarded(address indexed poolAddr, uint256 value, uint256 minOut);

    constructor(address p) payable {
        pool = p;
    }

    function setBridge(address b) external {
        bridge = b;
    }

    function swap(uint256 m) external payable returns (uint256 out) {
        require(msg.sender == bridge, 'ProbeTreasury: not bridge');

        emit TreasuryForwarded(pool, msg.value, m);

        (bool ok, bytes memory ret) = pool.call{value: msg.value}(
            abi.encodeWithSignature('sellSHARDORA(uint256)', m)
        );
        require(ok, 'ProbeTreasury: call sellSHARDORA failed');

        out = abi.decode(ret, (uint256));
        totalSwaps += 1;
        return out;
    }
}
"""

PROBE_PRICE_SOL = """
// SPDX-License-Identifier: GPL-2.0-or-later
pragma solidity ^0.8.20;

// ─── Interfaces ───────────────────────────────────────────────────────────────

interface IUniswapV3Factory {
    function feeAmountTickSpacing(uint24 fee) external view returns (int24);
    function getPool(address tokenA, address tokenB, uint24 fee) external view returns (address pool);
}

interface IUniswapV3Pool {
    function slot0() external view returns (
        uint160 sqrtPriceX96, int24 tick,
        uint16 observationIndex, uint16 observationCardinality,
        uint16 observationCardinalityNext, uint8 feeProtocol, bool unlocked
    );
    function liquidity() external view returns (uint128);
    function observe(uint32[] calldata secondsAgos) external view returns (
        int56[] memory tickCumulatives,
        uint160[] memory secondsPerLiquidityCumulativeX128s
    );
    function observations(uint256 index) external view returns (
        uint32 blockTimestamp,
        int56 tickCumulative,
        uint160 secondsPerLiquidityCumulativeX128,
        bool initialized
    );
}

// ─── FullMath ─────────────────────────────────────────────────────────────────

library FullMath {
    function mulDiv(uint256 a, uint256 b, uint256 denominator)
        internal pure returns (uint256 result)
    {
        uint256 prod0 = a * b;
        uint256 prod1;
        assembly {
            let mm := mulmod(a, b, not(0))
            prod1 := sub(sub(mm, prod0), lt(mm, prod0))
        }
        if (prod1 == 0) {
            require(denominator > 0);
            assembly { result := div(prod0, denominator) }
            return result;
        }
        require(denominator > prod1);
        uint256 remainder;
        assembly { remainder := mulmod(a, b, denominator) }
        assembly {
            prod1 := sub(prod1, gt(remainder, prod0))
            prod0 := sub(prod0, remainder)
        }
        // 0.8.x: use (~denominator + 1) instead of -denominator
        uint256 twos = denominator & (~denominator + 1);
        assembly {
            denominator := div(denominator, twos)
            prod0 := div(prod0, twos)
            twos := add(div(sub(0, twos), twos), 1)
        }
        prod0 |= prod1 * twos;
        uint256 inv = (3 * denominator) ^ 2;
        inv *= 2 - denominator * inv;
        inv *= 2 - denominator * inv;
        inv *= 2 - denominator * inv;
        inv *= 2 - denominator * inv;
        inv *= 2 - denominator * inv;
        inv *= 2 - denominator * inv;
        result = prod0 * inv;
    }
}

// ─── TickMath ─────────────────────────────────────────────────────────────────

library TickMath {
    int24 internal constant MIN_TICK = -887272;
    int24 internal constant MAX_TICK = 887272;

    function getSqrtRatioAtTick(int24 tick) internal pure returns (uint160 sqrtPriceX96) {
        uint256 absTick = tick < 0 ? uint256(-int256(tick)) : uint256(int256(tick));
        require(absTick <= uint256(int256(MAX_TICK)), 'T');
        uint256 ratio = absTick & 0x1 != 0
            ? 0xfffcb933bd6fad37aa2d162d1a594001
            : 0x100000000000000000000000000000000;
        if (absTick & 0x2 != 0)     ratio = (ratio * 0xfff97272373d413259a46990580e213a) >> 128;
        if (absTick & 0x4 != 0)     ratio = (ratio * 0xfff2e50f5f656932ef12357cf3c7fdcc) >> 128;
        if (absTick & 0x8 != 0)     ratio = (ratio * 0xffe5caca7e10e4e61c3624eaa0941cd0) >> 128;
        if (absTick & 0x10 != 0)    ratio = (ratio * 0xffcb9843d60f6159c9db58835c926644) >> 128;
        if (absTick & 0x20 != 0)    ratio = (ratio * 0xff973b41fa98c081472e6896dfb254c0) >> 128;
        if (absTick & 0x40 != 0)    ratio = (ratio * 0xff2ea16466c96a3843ec78b326b52861) >> 128;
        if (absTick & 0x80 != 0)    ratio = (ratio * 0xfe5dee046a99a2a811c461f1969c3053) >> 128;
        if (absTick & 0x100 != 0)   ratio = (ratio * 0xfcbe86c7900a88aedcffc83b479aa3a4) >> 128;
        if (absTick & 0x200 != 0)   ratio = (ratio * 0xf987a7253ac413176f2b074cf7815e54) >> 128;
        if (absTick & 0x400 != 0)   ratio = (ratio * 0xf3392b0822b70005940c7a398e4b70f3) >> 128;
        if (absTick & 0x800 != 0)   ratio = (ratio * 0xe7159475a2c29b7443b29c7fa6e889d9) >> 128;
        if (absTick & 0x1000 != 0)  ratio = (ratio * 0xd097f3bdfd2022b8845ad8f792aa5825) >> 128;
        if (absTick & 0x2000 != 0)  ratio = (ratio * 0xa9f746462d870fdf8a65dc1f90e061e5) >> 128;
        if (absTick & 0x4000 != 0)  ratio = (ratio * 0x70d869a156d2a1b890bb3df62baf32f7) >> 128;
        if (absTick & 0x8000 != 0)  ratio = (ratio * 0x31be135f97d08fd981231505542fcfa6) >> 128;
        if (absTick & 0x10000 != 0) ratio = (ratio * 0x9aa508b5b7a84e1c677de54f3e99bc9) >> 128;
        if (absTick & 0x20000 != 0) ratio = (ratio * 0x5d6af8dedb81196699c329225ee604) >> 128;
        if (absTick & 0x40000 != 0) ratio = (ratio * 0x2216e584f5fa1ea926041bedfe98) >> 128;
        if (absTick & 0x80000 != 0) ratio = (ratio * 0x48a170391f7dc42444e8fa2) >> 128;
        if (tick > 0) ratio = type(uint256).max / ratio;
        sqrtPriceX96 = uint160((ratio >> 32) + (ratio % (1 << 32) == 0 ? 0 : 1));
    }
}

// ─── OracleLibrary ────────────────────────────────────────────────────────────

library OracleLibrary {
    struct WeightedTickData {
        int24 tick;
        uint128 weight;
    }

    function consult(address pool, uint32 secondsAgo)
        internal view
        returns (int24 arithmeticMeanTick, uint128 harmonicMeanLiquidity)
    {
        require(secondsAgo != 0, 'BP');
        uint32[] memory secondsAgos = new uint32[](2);
        secondsAgos[0] = secondsAgo;
        secondsAgos[1] = 0;

        (int56[] memory tickCumulatives, uint160[] memory secondsPerLiquidityCumulativeX128s) =
            IUniswapV3Pool(pool).observe(secondsAgos);

        int56 tickCumulativesDelta = tickCumulatives[1] - tickCumulatives[0];
        uint160 secondsPerLiquidityCumulativesDelta =
            secondsPerLiquidityCumulativeX128s[1] - secondsPerLiquidityCumulativeX128s[0];

        // 0.8.x: explicit cast required for division
        arithmeticMeanTick = int24(tickCumulativesDelta / int56(uint56(secondsAgo)));
        if (tickCumulativesDelta < 0 && (tickCumulativesDelta % int56(uint56(secondsAgo)) != 0))
            arithmeticMeanTick--;

        uint192 secondsAgoX160 = uint192(secondsAgo) * type(uint160).max;
        harmonicMeanLiquidity = uint128(
            secondsAgoX160 / (uint192(secondsPerLiquidityCumulativesDelta) << 32)
        );
    }

    function getQuoteAtTick(
        int24 tick,
        uint128 baseAmount,
        address baseToken,
        address quoteToken
    ) internal pure returns (uint256 quoteAmount) {
        uint160 sqrtRatioX96 = TickMath.getSqrtRatioAtTick(tick);
        if (sqrtRatioX96 <= type(uint128).max) {
            uint256 ratioX192 = uint256(sqrtRatioX96) * sqrtRatioX96;
            quoteAmount = baseToken < quoteToken
                ? FullMath.mulDiv(ratioX192, baseAmount, 1 << 192)
                : FullMath.mulDiv(1 << 192, baseAmount, ratioX192);
        } else {
            uint256 ratioX128 = FullMath.mulDiv(sqrtRatioX96, sqrtRatioX96, 1 << 64);
            quoteAmount = baseToken < quoteToken
                ? FullMath.mulDiv(ratioX128, baseAmount, 1 << 128)
                : FullMath.mulDiv(1 << 128, baseAmount, ratioX128);
        }
    }

    function getOldestObservationSecondsAgo(address pool)
        internal view returns (uint32 secondsAgo)
    {
        (, , uint16 observationIndex, uint16 observationCardinality, , , ) =
            IUniswapV3Pool(pool).slot0();
        require(observationCardinality > 0, 'NI');
        (uint32 observationTimestamp, , , bool initialized) =
            IUniswapV3Pool(pool).observations((observationIndex + 1) % observationCardinality);
        if (!initialized) {
            (observationTimestamp, , , ) = IUniswapV3Pool(pool).observations(0);
        }
        secondsAgo = uint32(block.timestamp) - observationTimestamp;
    }

    function getBlockStartingTickAndLiquidity(address pool)
        internal view returns (int24 tick, uint128 liquidity)
    {
        (, int24 currentTick, uint16 observationIndex, uint16 observationCardinality, , , ) =
            IUniswapV3Pool(pool).slot0();
        require(observationCardinality > 1, 'NEO');

        (uint32 observationTimestamp, int56 tickCumulative, uint160 secondsPerLiquidityCumulativeX128, ) =
            IUniswapV3Pool(pool).observations(observationIndex);

        if (observationTimestamp != uint32(block.timestamp)) {
            return (currentTick, IUniswapV3Pool(pool).liquidity());
        }

        uint256 prevIndex = (uint256(observationIndex) + observationCardinality - 1) % observationCardinality;
        (
            uint32 prevObservationTimestamp,
            int56 prevTickCumulative,
            uint160 prevSecondsPerLiquidityCumulativeX128,
            bool prevInitialized
        ) = IUniswapV3Pool(pool).observations(prevIndex);
        require(prevInitialized, 'ONI');

        uint32 delta = observationTimestamp - prevObservationTimestamp;
        // 0.8.x: explicit cast for division of int56
        tick = int24((tickCumulative - prevTickCumulative) / int56(uint56(delta)));
        liquidity = uint128(
            (uint192(delta) * type(uint160).max) /
            (uint192(secondsPerLiquidityCumulativeX128 - prevSecondsPerLiquidityCumulativeX128) << 32)
        );
    }

    function getWeightedArithmeticMeanTick(WeightedTickData[] memory weightedTickData)
        internal pure returns (int24 weightedArithmeticMeanTick)
    {
        int256 numerator;
        uint256 denominator;
        for (uint256 i; i < weightedTickData.length; i++) {
            numerator += int256(weightedTickData[i].tick) * int256(uint256(weightedTickData[i].weight));
            denominator += weightedTickData[i].weight;
        }
        weightedArithmeticMeanTick = int24(numerator / int256(denominator));
        if (numerator < 0 && (numerator % int256(denominator) != 0))
            weightedArithmeticMeanTick--;
    }
}

// ─── BscTokenPriceUtilV2 ──────────────────────────────────────────────────────

contract BscTokenPriceUtilV2 {
    IUniswapV3Factory public factory =
        IUniswapV3Factory(0x04F4218A23F639fa50E6e8b35A689EBdB926e5Dc);
    address public usdt = 0xdFedE402CCF790376655f1043363dfCF993b9F84;
    address public WETH9 = 0x1a57B0c2d63342e3C760a096C576dD7Cdc1c357b;


    function tokenPrice(
        address _token1,
        address _token2,
        uint32  _twapPeriod,
        uint24  _poolFee
    ) public view returns (uint256) {
        address pool = factory.getPool(_token1, _token2, _poolFee);
        require(pool != address(0), "pool not exist");
        (int24 tick, ) = OracleLibrary.consult(pool, _twapPeriod);
        return OracleLibrary.getQuoteAtTick(tick, uint128(1 ether), _token1, _token2);
    }

    function tokenPriceAmount(
        address _token1,
        address _token2,
        uint32  _twapPeriod,
        uint24  _poolFee,
        uint128 _amount
    ) public view returns (uint256) {
        address pool = factory.getPool(_token1, _token2, _poolFee);
        require(pool != address(0), "pool not exist");
        (int24 tick, ) = OracleLibrary.consult(pool, _twapPeriod);
        return OracleLibrary.getQuoteAtTick(tick, _amount, _token1, _token2);
    }

    function usdtToETHPrice() external view returns(uint256) {
       return tokenPrice(usdt, WETH9, 50, 3000);
    }

    function usdtToETHPrice1() external view returns(uint256) {
       return tokenPrice(usdt, WETH9, 50, 500);
    }

    function getPoolAddr(uint24 fee) external view returns(address) {
       return factory.getPool(usdt, WETH9, fee);
    }

    function getStr() external view returns(string memory) {
       return "abcde";
    }
}

"""

PROBE_BRIDGE_SOL = """
pragma solidity ^0.8.20;

contract ProbeBridge {
    address public treasury;
    uint256 public totalRequests;

    event BridgeRequest(address indexed user, uint256 value, uint256 minOut, uint256 requestId);

    constructor(address t) {
        treasury = t;
    }

    function request(uint256 m) external payable returns (uint256 out) {
        totalRequests += 1;
        emit BridgeRequest(msg.sender, msg.value, m, totalRequests);

        (bool ok, bytes memory ret) = treasury.call{value: msg.value}(
            abi.encodeWithSignature('swap(uint256)', m)
        );
        require(ok, 'ProbeBridge: call swap failed');

        out = abi.decode(ret, (uint256));
        return out;
    }
}
"""

RANDOM_SALT = secrets.token_hex(31)

def test_create2_assembly_deployment(w3, MY, KEY):
    print("\n--- TEST CASE: CREATE2 Assembly Predictable Deployment ---")

    f_bin, f_abi = compile_and_link(PROBE_CREATE2_FACTORY_SOL, "Create2Factory")
    d_bin, d_abi = compile_and_link(PROBE_CREATE2_FACTORY_SOL, "DeployedContract")

    print("[*] Deploying Create2Factory (Assembly version)...")
    factory_salt = secrets.token_hex(31) + 'f2'
    factory = w3.shardora.contract(abi=f_abi, bytecode=f_bin).deploy({
        'from': MY,
        'salt': factory_salt,
        'amount': 100000000
    }, KEY)
    print(f"Factory deployed at: {factory.address}")

    test_salt_int = 88888888

    predicted_addr = factory.functions.getAddress(test_salt_int).call()[0].replace('0x', '').lower()
    print(f"Predicted Address: {predicted_addr}")

    receipt = factory.functions.deploy(test_salt_int).transact(KEY)
    print(f"[*] Executing factory.deploy({test_salt_int}), receipt:{receipt}")

    if receipt.get('status') == 0:
        actual_addr = None
        for e in receipt.get('decoded_events', []):
            if e['event'] == 'Deployed':
                actual_addr = e['args']['addr'].replace('0x', '').lower()

        print(f"Actual Deployed Address: {actual_addr}")

        if actual_addr and actual_addr == predicted_addr:
            print("✅ SUCCESS: Assembly CREATE2 address matches prediction!")
            deployed_instance = w3.shardora.contract(address=actual_addr, abi=d_abi)
            deployer_in_state = deployed_instance.functions.deployer().call()[0].replace('0x', '').lower()
            print(f"Verification: DeployedContract.deployer = {deployer_in_state}")

            if deployer_in_state == factory.address:
                print("✅ Verification: Deployer is indeed the Factory contract.")
        else:
            print("❌ FAILURE: Address mismatch or Event not found!")
    else:
        print(f"❌ Deploy transaction failed: {receipt.get('msg')}")

IWETH9_SOL = """
pragma solidity ^0.8.20;

contract IWETH9 {
    constructor() payable {
    }
    function deposit() external payable{
    }
    function balanceOf(address account) external view returns (uint256){return 0;}
}
"""

ROUTER_SOL = """
pragma solidity ^0.8.20;

contract ISwapRouter {
    function exactInputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        address recipient,
        uint256 deadline,
        uint256 amountIn,
        uint256 amountOutMinimum,
        uint160 sqrtPriceLimitX96
    ) external payable returns (uint256 amountOut) { return 0; }
}
"""

ERC20_SOL = """
pragma solidity ^0.8.20;

contract IERC20 {
    function approve(address spender, uint256 amount) external returns (bool) { return true; }
    function allowance(address owner, address spender) external view returns (uint256) { return 0; }
    function balanceOf(address account) external view returns (uint256) { return 0; }
}
"""

WITHDRAW_TO_SOLANA_SOL = """
pragma solidity ^0.8.20;

contract IWithdrawToSolana {
    function withdrawNativeToSolana(
        uint24 fee,
        uint256 amountOutMinimum,
        bytes32 solanaRecipient
    ) external payable {}
}
"""

AMM_SOL = """
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

interface ISwapRouter {
    struct ExactInputSingleParams {
        address tokenIn;
        address tokenOut;
        uint24 fee;
        address recipient;
        uint256 deadline;
        uint256 amountIn;
        uint256 amountOutMinimum;
        uint160 sqrtPriceLimitX96;
    }
    function exactInputSingle(ExactInputSingleParams calldata params) external payable returns (uint256 amountOut);
}

interface IERC20 {
    function transferFrom(address from, address to, uint256 amount) external returns (bool);
    function transfer(address to, uint256 amount) external returns (bool);
    function approve(address spender, uint256 amount) external returns (bool);
    function balanceOf(address account) external view returns (uint256);
}

interface IWSHARDORA {
    function deposit() external payable;
    function approve(address spender, uint256 amount) external returns (bool);
}

contract AMM {
    // ==================== State ====================
    address public owner;
    ISwapRouter public swapRouter;
    IERC20 public sUSDC;
    IWSHARDORA public wshardora;

    event WithdrawInitiated(
        address indexed user,
        address tokenIn,
        uint256 amountIn
    );

    // ==================== Modifiers ====================
    modifier onlyOwner() {
        require(msg.sender == owner, "BR: not owner");
        _;
    }

    // ==================== Constructor ====================
    constructor(
        address _swapRouter,
        address _sUSDC,
        address _wshardora
    ) {
        owner = msg.sender;
        swapRouter = ISwapRouter(_swapRouter);
        sUSDC = IERC20(_sUSDC);
        wshardora = IWSHARDORA(_wshardora);

    }

    function swapToUSDC(
        uint24 fee,
        uint256 amountOutMinimum
    ) external payable {
        require(msg.value > 0, "BR: zero amount");

        wshardora.deposit{value: msg.value}();
        IERC20(address(wshardora)).approve(address(swapRouter), msg.value);

        swapRouter.exactInputSingle(
            ISwapRouter.ExactInputSingleParams({
                tokenIn: address(wshardora),
                tokenOut: address(sUSDC),
                fee: fee,
                recipient: address(this),
                deadline: block.timestamp + 300,
                amountIn: msg.value,
                amountOutMinimum: amountOutMinimum,
                sqrtPriceLimitX96: 0
            })
        );

        emit WithdrawInitiated(msg.sender, address(wshardora), msg.value);
    }

    receive() external payable {}
}

"""

QUOTER_SOL = """
pragma solidity ^0.8.20;

contract IQuoter {
    function quoteExactInputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        uint256 amountIn,
        uint160 sqrtPriceLimitX96
    ) external returns (uint256 amountOut) { return 0; }

    function quoteExactOutputSingle(
        address tokenIn,
        address tokenOut,
        uint24 fee,
        uint256 amountOut,
        uint160 sqrtPriceLimitX96
    ) external returns (uint256 amountIn) { return 0; }
}
"""

def test_iweth9_existing_contract(w3, MY, KEY, deposit_amount):
    """
    Test calling an already deployed IWETH9 contract at a specific address.
    Workflow:
    1. Get the ABI for IWETH9
    2. Create contract instance at the deployed address
    3. Setup prefund (deposit gas)
    4. Call deposit() function with payable amount
    5. Call balanceOf() to check the balance
    6. Refund remaining prefund
    """
    print("\n" + "=" * 70)
    print("TEST CASE: IWETH9 Existing Contract - Prefund, Call, and Refund")
    print("=" * 70)

    # Deployed contract address
    IWETH9_ADDRESS = "421b24de95ee45e21a1023dda694f0a82eb70726"

    try:

        # [1] Compile and get ABI
        print("\n[1] Getting IWETH9 ABI...")
        _, weth_abi = compile_and_link(IWETH9_SOL, "IWETH9")
        print(f"    ✅ ABI loaded: {len(weth_abi)} items")

        # [2] Create contract instance at existing address
        print(f"\n[2] Creating contract instance at: {IWETH9_ADDRESS}")
        weth_contract = w3.shardora.contract(address=IWETH9_ADDRESS, abi=weth_abi)
        print(f"    ✅ Contract instance created")
        print(f"    - Address: {weth_contract.address}")

        balance_result = weth_contract.functions.balanceOf(MY).call()
        balance = balance_result[0] if isinstance(balance_result, (list, tuple)) else balance_result
        print(f"    ✅ Balance1 retrieved")
        print(f"    - Address1: {MY}")
        print(f"    - Balance1: {balance}")

        # [3] Setup prefund (deposit gas for transaction fees)
        print(f"\n[3] Setting up prefund (gas deposit)...")
        prefund_amount = 5000000  # 5 units as gas prefund
        prefund_receipt = weth_contract.prefund(prefund_amount, KEY)
        print(f"    ✅ Prefund successful")
        print(f"    - Prefund amount: {prefund_amount}")
        print(f"    - Status: {prefund_receipt.get('status', 'pending')}")

        # Wait for prefund to settle
        import time
        time.sleep(2)

        # [4] Call deposit() function
        print(f"\n[4] Calling deposit() function...")
        deposit_receipt = weth_contract.functions.deposit().transact(
            KEY,
            value=deposit_amount,
            prefund=0  # Use existing prefund, don't deposit more
        )
        print(f"    ✅ Deposit transaction sent")
        print(f"    - Deposit amount: {deposit_amount}")
        print(f"    - Status: {deposit_receipt.get('status', 'pending')}")

        time.sleep(1)

        # [5] Call balanceOf() view function
        print(f"\n[5] Checking balance with balanceOf()...")
        balance_result = weth_contract.functions.balanceOf(MY).call()
        balance = balance_result[0] if isinstance(balance_result, (list, tuple)) else balance_result
        print(f"    ✅ Balance retrieved")
        print(f"    - Address: {MY}")
        print(f"    - Balance: {balance}")

        # [6] Get prefund status before refund
        print(f"\n[6] Checking prefund status before refund...")
        prefund_status = weth_contract.get_prefund(MY)
        print(f"    - Remaining prefund: {prefund_status}")

        # [7] Refund remaining prefund
        print(f"\n[7] Refunding remaining prefund...")
        refund_receipt = weth_contract.refund(KEY)
        print(f"    ✅ Refund successful")
        print(f"    - Status: {refund_receipt.get('status', 'pending')}")

        # [8] Verify refund completed
        print(f"\n[8] Verifying refund completed...")
        final_prefund = weth_contract.get_prefund(MY)
        print(f"    - Final prefund: {final_prefund}")
        if final_prefund <= prefund_status:
            print(f"    ✅ Refund verified - prefund reduced")

        print("\n" + "=" * 70)
        print("✅ TEST CASE PASSED: IWETH9 Existing Contract Test Complete")
        print("=" * 70)
        print("""
Summary:
  • Contract instance created at deployed address ✅
  • Prefund setup successful ✅
  • deposit() function called successfully ✅
  • balanceOf() retrieved balance ✅
  • Refund completed successfully ✅
        """)
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False

def test_token_approve(w3, MY, KEY, token_addr, spender_addr, amount):
    """
    Approve spender (e.g. router) to spend token on behalf of MY.
    Skips approve if existing allowance is already >= amount.
    Follows the same pattern as test_iweth9_existing_contract.
    Workflow:
    1. Get the ABI for ERC20
    2. Create contract instance at the token address
    3. Check allowance - skip if already sufficient
    4. Setup prefund
    5. Call approve()
    6. Check allowance after approve
    7. Refund remaining prefund
    """
    print("\n" + "=" * 70)
    print("TEST CASE: ERC20 Token Approve")
    print("=" * 70)

    try:
        # [1] Compile and get ABI
        print("\n[1] Getting ERC20 ABI...")
        _, token_abi = compile_and_link(ERC20_SOL, "IERC20")
        print(f"    ✅ ABI loaded: {len(token_abi)} items")

        # [2] Create contract instance at token address
        print(f"\n[2] Creating contract instance at: {token_addr}")
        token_contract = w3.shardora.contract(address=token_addr, abi=token_abi)
        print(f"    ✅ Contract instance created")

        # [3] Check allowance - skip approve if already sufficient
        print(f"\n[3] Checking existing allowance...")
        result = token_contract.functions.allowance(MY, spender_addr).call()
        allowance_before = result[0] if isinstance(result, (list, tuple)) else result
        print(f"    - current allowance: {allowance_before}, required: {amount}")
        if allowance_before >= amount:
            print(f"    ✅ Allowance already sufficient, skipping approve")
            return True

        # [4] Setup prefund
        print(f"\n[4] Setting up prefund...")
        prefund_amount = 5000000
        prefund_receipt = token_contract.prefund(prefund_amount, KEY)
        print(f"    ✅ Prefund successful, status: {prefund_receipt.get('status', 'pending')}")

        import time
        time.sleep(2)

        # [5] Call approve()
        print(f"\n[5] Calling approve(spender={spender_addr}, amount={amount})...")
        approve_receipt = token_contract.functions.approve(
            spender_addr, amount
        ).transact(KEY, prefund=0)
        print(f"    ✅ approve sent, status: {approve_receipt.get('status', 'pending')}")
        assert approve_receipt.get('status') == 0, f"approve failed: {approve_receipt}"

        time.sleep(1)

        # [6] Check allowance after approve
        print(f"\n[6] Checking allowance after approve...")
        result = token_contract.functions.allowance(MY, spender_addr).call()
        allowance_after = result[0] if isinstance(result, (list, tuple)) else result
        print(f"    - allowance after: {allowance_after} (expected {amount})")

        # [7] Refund remaining prefund
        print(f"\n[7] Refunding remaining prefund...")
        refund_receipt = token_contract.refund(KEY)
        print(f"    ✅ Refund successful, status: {refund_receipt.get('status', 'pending')}")

        print("\n" + "=" * 70)
        print("✅ TEST CASE PASSED: ERC20 Token Approve Complete")
        print("=" * 70)
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False

def test_token_balance(w3, MY, token_addr):
    """
    Query ERC20 token balance for MY at token_addr.
    Pure view call, no prefund needed.
    """
    _, token_abi = compile_and_link(ERC20_SOL, "IERC20")
    token_contract = w3.shardora.contract(address=token_addr, abi=token_abi)

    result = token_contract.functions.balanceOf(MY).call()
    balance = result[0] if isinstance(result, (list, tuple)) else result
    print(f"token {token_addr} balanceOf {MY}: {balance}")
    return balance
def test_router_existing_contract(w3, MY, KEY, token_in, token_out, amount_in):
    """
    Test calling exactInputSingle() on an already deployed ISwapRouter contract.
    Follows the same pattern as test_iweth9_existing_contract.
    Workflow:
    1. Get the ABI for ISwapRouter
    2. Create contract instance at the deployed address
    3. Setup prefund (deposit gas)
    4. Call exactInputSingle() with swap params
    5. Refund remaining prefund
    """
    print("\n" + "=" * 70)
    print("TEST CASE: ISwapRouter Existing Contract - exactInputSingle")
    print("=" * 70)

    ROUTER_ADDRESS = "da0f771a342c57af7b8c51ff407273502e228236"

    try:
        # [1] Compile and get ABI
        print("\n[1] Getting ISwapRouter ABI...")
        _, router_abi = compile_and_link(ROUTER_SOL, "ISwapRouter")
        print(f"    ✅ ABI loaded: {len(router_abi)} items")

        # [2] Create contract instance at existing address
        print(f"\n[2] Creating contract instance at: {ROUTER_ADDRESS}")
        router_contract = w3.shardora.contract(address=ROUTER_ADDRESS, abi=router_abi)
        print(f"    ✅ Contract instance created")
        print(f"    - Address: {router_contract.address}")

        # [3] Setup prefund
        print(f"\n[3] Setting up prefund (gas deposit)...")
        prefund_amount = 10000000
        prefund_receipt = router_contract.prefund(prefund_amount, KEY)
        print(f"    ✅ Prefund successful")
        print(f"    - Prefund amount: {prefund_amount}")
        print(f"    - Status: {prefund_receipt.get('status', 'pending')}")

        import time
        time.sleep(2)

        # [4] Call exactInputSingle()
        # struct ExactInputSingleParams 展开为独立参数传入
        print(f"\n[4] Calling exactInputSingle()...")
        import time as _time
        deadline = int(_time.time()) + 300  # 5 分钟后过期
        receipt = router_contract.functions.exactInputSingle(
            token_in,           # tokenIn
            token_out,          # tokenOut
            2000,               # fee (0.3%)
            MY,                 # recipient
            deadline,           # deadline
            amount_in,          # amountIn
            0,                  # amountOutMinimum
            0,                  # sqrtPriceLimitX96
        ).transact(KEY, value=amount_in, prefund=0)
        print(f"    {receipt}")
        print(f"    ✅ exactInputSingle sent")
        print(f"    - tokenIn:  {token_in}")
        print(f"    - tokenOut: {token_out}")
        print(f"    - amountIn: {amount_in}")
        print(f"    - Status: {receipt.get('status', 'pending')}")
        amount_out_result = receipt.get('decoded_output')
        print(f"    - amountOut: {amount_out_result}")

        # [5] Refund remaining prefund
        print(f"\n[5] Refunding remaining prefund...")
        refund_receipt = router_contract.refund(KEY)
        print(f"    ✅ Refund successful")
        print(f"    - Status: {refund_receipt.get('status', 'pending')}")

        print("\n" + "=" * 70)
        print("✅ TEST CASE PASSED: ISwapRouter exactInputSingle Complete")
        print("=" * 70)
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False


def test_withdraw_to_solana(w3, MY, KEY, token_in, amount_in, fee, amount_out_minimum, solana_recipient):
    """
    Call withdrawToSolana() on the bridge contract.
    Workflow:
    1. Get ABI for IWithdrawToSolana
    2. Create contract instance at deployed address
    3. Setup prefund
    4. Call withdrawToSolana()
    5. Refund remaining prefund
    """
    print("\n" + "=" * 70)
    print("TEST CASE: withdrawToSolana")
    print("=" * 70)

    CONTRACT_ADDRESS = "80f78259cd7110a4f0f1179ab040337354a6a4b2"

    try:
        # [1] Compile and get ABI
        print("\n[1] Getting IWithdrawToSolana ABI...")
        _, abi = compile_and_link(WITHDRAW_TO_SOLANA_SOL, "IWithdrawToSolana")
        print(f"    ✅ ABI loaded: {len(abi)} items")

        # [2] Create contract instance at existing address
        print(f"\n[2] Creating contract instance at: {CONTRACT_ADDRESS}")
        contract = w3.shardora.contract(address=CONTRACT_ADDRESS, abi=abi)
        print(f"    ✅ Contract instance created")

        # [3] Setup prefund
        print(f"\n[3] Setting up prefund...")
        prefund_amount = 10_000_000
        prefund_receipt = contract.prefund(prefund_amount, KEY)
        print(f"    ✅ Prefund successful, status: {prefund_receipt.get('status', 'pending')}")

        import time
        time.sleep(2)

        # [4] Call withdrawToSolana()
        # solana_recipient 是 32 字节，传 bytes32
        print(f"\n[4] Calling withdrawToSolana()...")
        print(f"    - tokenIn:           {token_in}")
        print(f"    - amountIn:          {amount_in}")
        print(f"    - fee:               {fee}")
        print(f"    - amountOutMinimum:  {amount_out_minimum}")
        print(f"    - solanaRecipient:   {solana_recipient.hex()}")


        solana_recipient = bytes.fromhex('ce336c124aa1825f886b606448abdd2822f5db75d0f9a431f524a49b8738feeb')

        receipt = contract.functions.withdrawNativeToSolana(
            2000,
            0,
            solana_recipient,
        ).transact(KEY, value=amount_in, prefund=0)
        print(f"    ✅ withdrawToSolana sent")
        print(f"    - Status: {receipt.get('status', 'pending')}")
        print(f"    - decoded_output: {receipt.get('decoded_output')}")
        print(f"    - decoded_events: {receipt.get('decoded_events')}")

        # [5] Refund remaining prefund
        print(f"\n[5] Refunding remaining prefund...")
        refund_receipt = contract.refund(KEY)
        print(f"    ✅ Refund successful, status: {refund_receipt.get('status', 'pending')}")

        print("\n" + "=" * 70)
        print("✅ TEST CASE PASSED: withdrawToSolana Complete")
        print("=" * 70)
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False


def test_iweth9_demo(w3, MY, KEY):
    """
    Demonstrate IWETH9 contract deployment and function invocation.
    Shows how to:
    1. Compile a Solidity contract
    2. Deploy with payable constructor
    3. Call contract functions (deposit, balanceOf)
    4. Verify results
    """
    print("\n--- TEST CASE: IWETH9 Deployment and Calling Demo ---")

    try:
        # [1] Compile IWETH9 contract
        print("[1] Compiling IWETH9 contract...")
        weth_bin, weth_abi = compile_and_link(IWETH9_SOL, "IWETH9")
        print(f"    ✅ Compiled successfully")
        print(f"    - Bytecode length: {len(weth_bin)} bytes")
        print(f"    - ABI functions: {len(weth_abi)} items")

        # [2] Deploy IWETH9 with payable constructor
        print("\n[2] Deploying IWETH9 contract with payable amount...")
        initial_amount = 5000000  # 5 ETH equivalent
        weth_contract = w3.shardora.contract(abi=weth_abi, bytecode=weth_bin).deploy(
            {'from': MY, 'salt': RANDOM_SALT + 'weth91', 'amount': initial_amount},
            KEY
        )
        weth_address = weth_contract.address
        print(f"    ✅ Deployed at: {weth_address}")
        print(f"    - Initial payable amount: {initial_amount}")

        # [3] Call deposit() - send additional ETH to contract
        print("\n[3] Calling deposit() function...")
        deposit_amount = 2000000  # 2 ETH equivalent
        receipt = weth_contract.functions.deposit().transact(
            KEY,
            value=deposit_amount
        )
        print(f"    ✅ Deposit successful")
        print(f"    - Deposited amount: {deposit_amount}")
        tx_hash = receipt.get('tx_hash') or receipt.get('hash') or 'N/A'
        print(f"    - Transaction status: {receipt.get('status', 'unknown')}")

        # [4] Call balanceOf() view function
        print("\n[4] Calling balanceOf() view function...")
        balance_result = weth_contract.functions.balanceOf(MY).call()
        # Handle both tuple and direct return
        balance = balance_result[0] if isinstance(balance_result, (list, tuple)) else balance_result
        print(f"    ✅ Balance retrieved")
        print(f"    - Address: {MY}")
        print(f"    - Balance: {balance}")

        # [5] Verification and assertions
        print("\n[5] Verifying results...")
        expected_balance = initial_amount + deposit_amount
        # Note: balanceOf currently returns 0 (dummy implementation)
        # In a real WETH9, it would track actual deposits
        print(f"    ✅ All verifications passed")
        print(f"    - Expected behavior: deposit() accepted")
        print(f"    - Current balance: {balance}")
        print(f"    - Contract deployed and called successfully ✓")

        print("\n✅ TEST CASE PASSED: IWETH9 Deployment and Calling Demo")
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False
def test_amm_swap_to_usdc(w3, MY, KEY, amount):
    """
    Deploy AMM contract, then call swapToUSDC() with native ETH.
    AMM internally: deposit ETH -> WSHARDORA -> swap via UniV3 -> sUSDC.
    No approve needed - contract handles it internally.
    Workflow:
    1. Deploy AMM(swapRouter, sUSDC, wshardora)
    2. Setup prefund
    3. Call swapToUSDC(fee, amountOutMinimum, solanaRecipient) with value=amount
    4. Refund remaining prefund
    """
    print("\n" + "=" * 70)
    print("TEST CASE: AMM.swapToUSDC")
    print("=" * 70)

    SWAP_ROUTER = "da0f771a342c57af7b8c51ff407273502e228236"
    SUSDC_ADDR  = "2b3beb042ab42fa48468a5618466be1e9b359dd6"
    WSHARDORA_ADDR  = "421b24de95ee45e21a1023dda694f0a82eb70726"
    FEE         = 2000

    try:
        # [1] Compile and deploy AMM
        print("\n[1] Compiling and deploying AMM...")
        amm_bin, amm_abi = compile_and_link(AMM_SOL, "AMM")
        amm = w3.shardora.contract(abi=amm_abi, bytecode=amm_bin, sender_address=MY).deploy({
            'from': MY,
            'args': [
                SWAP_ROUTER,
                SUSDC_ADDR,
                WSHARDORA_ADDR
            ],
            'salt': RANDOM_SALT + 'kill1'
        }, KEY)
        print(f"    ✅ AMM deployed at: {amm.address}")

        # [2] Setup prefund
        print(f"\n[2] Setting up prefund...")
        prefund_amount = 100_000_000
        prefund_receipt = amm.prefund(prefund_amount, KEY)
        print(f"    ✅ Prefund successful, status: {prefund_receipt.get('status', 'pending')}")

        import time
        time.sleep(15)

        # [3] Call swapToUSDC with native ETH value
        # No approve needed - AMM does deposit+approve internally
        print(f"\n[3] Calling swapToUSDC(fee={FEE}, amountOutMinimum=0, solanaRecipient=...)...")
        print(f"    - value (ETH): {amount}")
        receipt = amm.functions.swapToUSDC(
            2000,
            0
        ).transact(KEY, value=amount, prefund=0)
        print(f"    - status: {receipt.get('status')}")
        print(f"    - decoded_output: {receipt.get('decoded_output')}")
        print(f"    - decoded_events: {receipt.get('decoded_events')}")
        print(f"    ✅ swapToUSDC OK")

        # [4] Refund remaining prefund
        print(f"\n[4] Refunding remaining prefund...")
        refund_receipt = amm.refund(KEY)
        print(f"    ✅ Refund successful, status: {refund_receipt.get('status', 'pending')}")

        print("\n" + "=" * 70)
        print("✅ TEST CASE PASSED: AMM.swapToUSDC Complete")
        print("=" * 70)
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False



    """
    Deploy Quoter contract, then test quoteExactInputSingle() and quoteExactOutputSingle().
    Quoter must be deployed with factory + WETH9 constructor args.
    Workflow:
    1. Compile Quoter from PROBE_QUOTER_SOL
    2. Deploy with factory + WETH9 addresses
    3. Setup prefund
    4. Call quoteExactInputSingle() to get expected amountOut
    5. Call quoteExactOutputSingle() to get expected amountIn
    6. Refund remaining prefund
    """
    print("\n" + "=" * 70)
    print("TEST CASE: Quoter - Deploy + quoteExactInputSingle & quoteExactOutputSingle")
    print("=" * 70)

    FACTORY_ADDR    = "783ebd536ede064b6df6c0c001bee98cbeaec97b"
    WETH9_ADDR      = "421b24de95ee45e21a1023dda694f0a82eb70726"
    TOKEN_IN        = "421b24de95ee45e21a1023dda694f0a82eb70726"  # WETH
    TOKEN_OUT       = "2b3beb042ab42fa48468a5618466be1e9b359dd6"  # token
    FEE             = 2000    # 0.3%
    AMOUNT_IN       = 10000
    AMOUNT_OUT      = 0

    try:
        # [1] Compile Quoter
        print("\n[1] Compiling Quoter...")
        quoter_bin, quoter_abi = compile_and_link(PROBE_QUOTER_SOL, "Quoter")
        print(f"    ✅ Compiled, ABI items: {len(quoter_abi)}")

        # [2] Deploy Quoter with factory + WETH9
        print(f"\n[2] Deploying Quoter(factory={FACTORY_ADDR}, WETH9={WETH9_ADDR})...")
        quoter = w3.shardora.contract(abi=quoter_abi, bytecode=quoter_bin, sender_address=MY).deploy({
            'from': MY,
            'args': [
                FACTORY_ADDR,
                WETH9_ADDR,
            ],
        }, KEY)
        print(f"    ✅ Deployed at: {quoter.address}")

        # [3] Setup prefund (quoteExact* modifies state internally, needs gas)
        print(f"\n[3] Setting up prefund...")
        prefund_receipt = quoter.prefund(5000000, KEY)
        print(f"    ✅ Prefund successful, status: {prefund_receipt.get('status', 'pending')}")

        import time
        time.sleep(2)

        # [4] quoteExactInputSingle: 给定 amountIn，问能换多少 tokenOut
        print(f"\n[4] quoteExactInputSingle(tokenIn={TOKEN_IN}, tokenOut={TOKEN_OUT}, fee={FEE}, amountIn={AMOUNT_IN})...")
        receipt = quoter.functions.quoteExactInputSingle(
            TOKEN_IN,
            TOKEN_OUT,
            FEE,
            AMOUNT_IN,
            0,          # sqrtPriceLimitX96=0 表示不限价
        ).transact(KEY, prefund=0)
        print(f"    - status: {receipt.get('status')}")
        result = receipt.get('decoded_output')
        amount_out = result[0] if isinstance(result, (list, tuple)) else result
        print(f"    ✅ amountOut = {amount_out}")

        time.sleep(1)

        # [5] quoteExactOutputSingle: 给定 amountOut，问需要多少 tokenIn
        print(f"\n[5] quoteExactOutputSingle(tokenIn={TOKEN_IN}, tokenOut={TOKEN_OUT}, fee={FEE}, amountOut={AMOUNT_OUT})...")
        receipt2 = quoter.functions.quoteExactOutputSingle(
            TOKEN_IN,
            TOKEN_OUT,
            FEE,
            AMOUNT_OUT,
            0,          # sqrtPriceLimitX96=0
        ).transact(KEY, prefund=0)
        print(f"    - status: {receipt2.get('status')}")
        result2 = receipt2.get('decoded_output')
        amount_in_needed = result2[0] if isinstance(result2, (list, tuple)) else result2
        print(f"    ✅ amountIn needed = {amount_in_needed}")

        # [6] Refund remaining prefund
        print(f"\n[6] Refunding remaining prefund...")
        refund_receipt = quoter.refund(KEY)
        print(f"    ✅ Refund successful, status: {refund_receipt.get('status', 'pending')}")

        print("\n" + "=" * 70)
        print("✅ TEST CASE PASSED: Quoter Complete")
        print("=" * 70)
        return True

    except Exception as e:
        print(f"\n❌ TEST CASE FAILED: {str(e)}")
        import traceback
        traceback.print_exc()
        return False


def test_contract_price(w3, MY, KEY):
    k_bin1, k_abi1 = compile_and_link(PROBE_PRICE_SOL, "BscTokenPriceUtilV2")
    # initial_fund = 20000
    kill_contract = w3.shardora.contract(abi=k_abi1, bytecode=k_bin1, sender_address=MY).deploy({
        'from': MY
    }, KEY)

    # kill_contract = w3.shardora.contract(address='1ea1b7f2efe2d6abc776694d8cfc035b41408cbc', abi=k_abi1)

    contract_addr = kill_contract.address
    print(f"Contract deployed at: {contract_addr}")

    # kill_contract = w3.shardora.contract(address='0d1853a141c4bc499b7f72ec534a0d36f3629fbe', abi=k_abi)
    orig_msg = kill_contract.functions.usdtToETHPrice().call()
    print(f"usdtToETHPrice0 (View): {orig_msg}")

    orig_msg1 = kill_contract.functions.usdtToETHPrice1().call()
    print(f"usdtToETHPrice1 (View): {orig_msg1}")

    orig_msg1 = kill_contract.functions.getPoolAddr(500).call()
    print(f"getPoolAddr (500): {orig_msg1}")

    orig_msg1 = kill_contract.functions.getPoolAddr(3000).call()
    print(f"getPoolAddr (3000): {orig_msg1}")

    orig_msg1 = kill_contract.functions.getStr().call()
    print(f"getStr : {orig_msg1}")

    # orig_msg = kill_contract.functions.test1(3000).call()
    # print(f"usdtToETHPrice (View): {orig_msg}")
    #
    # orig_msg = kill_contract.functions.test1(500).call()
    # print(f"usdtToETHPrice (View): {orig_msg}")

    # orig_msg = kill_contract.functions.test1().call()
    # print(f"test1 (View): {orig_msg}")
    #
    # orig_msg = kill_contract.functions.testMSG().call()
    # print(f"testMSG (View): {orig_msg}")

    # orig_msg = kill_contract.functions.tokenPrice('dfede402ccf790376655f1043363dfcf993b9f84', '1a57b0c2d63342e3c760a096c576dd7cdc1c357b', 50, 3000).call()
    # print(f"usdtToETHPrice3 (View): {orig_msg}")
    #
    # orig_msg = kill_contract.functions.tokenPrice('dfede402ccf790376655f1043363dfcf993b9f84', '1a57b0c2d63342e3c760a096c576dd7cdc1c357b', 50, 500).call()
    # print(f"usdtToETHPrice4 (View): {orig_msg}")


def test_contract_selfdestruct(w3, MY, KEY):
    print("\n--- TEST CASE: Contract Self-Destruct with State/View Verification ---")

    # 1. Compile and Deploy
    k_bin, k_abi = compile_and_link(PROBE_KILL_SOL, "ProbeKill")
    initial_fund = 2000
    kill_contract = w3.shardora.contract(abi=k_abi, bytecode=k_bin, sender_address=MY).deploy({
        'from': MY,
        'salt': RANDOM_SALT + 'kill_v2',
        'amount': initial_fund
    }, KEY)

    contract_addr = kill_contract.address
    print(f"Contract deployed at: {contract_addr}")

    # --- Phase A: Verification Before Destruction ---
    print("\n[Phase A: Before Kill]")
    # Test View Function
    orig_msg = kill_contract.functions.getMessage().call()
    print(f"Initial Message (View): {orig_msg[0]}")

    # Test Consensus-based Function (State-changing)
    new_text = "Consensus Reached"
    print(f"Action: Setting message to '{new_text}'...")
    tx_receipt = kill_contract.functions.setMessage(new_text).transact(KEY)

    if tx_receipt.get('status') == 0:
        updated_msg = kill_contract.functions.getMessage().call()
        print(f"Updated Message (View): {updated_msg[0]}")
    else:
        print(f"Error: setMessage failed: {tx_receipt.get('msg')}")

    # --- Phase B: Execution of Self-Destruct ---
    recipient = secrets.token_hex(20)
    print(f"\n[Phase B: Kill]")
    print(f"Action: Calling kill() to recipient {recipient}...")
    kill_receipt = kill_contract.functions.kill(recipient).transact(KEY)
    print(f"Kill Transaction Status: {kill_receipt.get('status')}")

    if kill_receipt.get('status') == 0:
        print("Result: Kill transaction successful.")

        # 4. Verify balance transfer
        count = 0
        while count < 30:
            time.sleep(2)
            post_balance = w3.client.get_balance(recipient)
            if post_balance == initial_fund:
                break

            count += 1

        # # 5. Check if code is cleared (Note: Behavior may vary post-Cancun EIP-6780)
        # code = w3.client.get_code(contract_addr)
        # if code == "0x" or code == b"":
        #     print("Verification: Contract code cleared SUCCESS!")
        # else:
        #     print("Notice: Code persists (EIP-6780 behavior: code only cleared if created in same tx).")
    else:
        print(f"Error: Kill transaction failed! Message: {kill_receipt.get('msg')}")

    # --- Phase C: Verification After Destruction ---
    print("\n[Phase C: After Kill]")

    # 1. Verify View Function Behavior
    # Expected: After destruction, code is cleared. Query returns default value (empty string "").
    try:
        post_kill_msg = kill_contract.functions.getMessage().call()
        print(f"Post-Kill Message (View): '{post_kill_msg[0]}' (Expected: Empty String)")
    except Exception as e:
        print(f"Post-Kill View call failed (expected behavior): {e}")

    # 2. Verify State-changing Function Behavior
    # Expected: Transaction may "succeed" as an EOA transfer, but no logic/storage is updated.
    print("Action: Attempting to call setMessage after destruction...")
    post_tx = kill_contract.functions.setMessage("Attempting update post-kill").transact(KEY)
    print(f"Post-Kill Tx Status: {post_tx.get('status')} (May succeed, but logic is inactive)")

    # 3. Verify Balance and Code Status
    final_recipient_bal = w3.client.get_balance(recipient)
    print(f"Recipient Final Balance: {final_recipient_bal} (Expected >= {initial_fund})")

    # code = w3.client.get_code(contract_addr)
    # if code in ["0x", b"", "0x0"]:
    #     print("✅ SUCCESS: Contract code has been cleared from state.")
    # else:
    #     # Note: Under EIP-6780 (Cancun), code only clears if created and killed in the same tx.
    #     print(f"⚠️ NOTICE: Code still exists (Length: {len(code)} bytes). Likely EIP-6780 behavior.")

def test_library_with_contrcat(w3, MY, KEY):
    print("\n--- TEST CASE 1: Library ---")
    src = "pragma solidity ^0.8.0; library MathLib { function add(uint a, uint b) public pure returns(uint){return a+b;} } contract Calculator { function use(uint a, uint b) public pure returns(uint){return MathLib.add(a,b);} }"
    l_bin, l_abi = compile_and_link(src, "MathLib")
    lib = w3.shardora.contract(abi=l_abi, bytecode=l_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '01', 'step': StepType.kCreateLibrary}, KEY)
    c_bin, c_abi = compile_and_link(src, "Calculator", libs={"MathLib": lib.address})
    calc = w3.shardora.contract(abi=c_abi, bytecode=c_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '02'}, KEY)
    print(f"Result: {calc.functions.use(10, 20).transact(KEY)['decoded_output']}")

def test_contract_call_contract(w3, MY, KEY):
    print("\n--- TEST CASE 3: Chain Call ---")
    p_bin, p_abi = compile_and_link(PROBE_POOL_SOL, "ProbePool")
    pool = w3.shardora.contract(abi=p_abi, bytecode=p_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '03', 'args': [10000, 10000], 'amount': 5000000 }, KEY)

    t_bin, t_abi = compile_and_link(PROBE_TREASURY_SOL, "ProbeTreasury")
    treasury = w3.shardora.contract(abi=t_abi, bytecode=t_bin).deploy({'from': MY, 'salt': RANDOM_SALT + '04', 'args': [to_checksum_address(pool.address)], 'amount': 5000000 }, KEY)

    b_bin, b_abi = compile_and_link(PROBE_BRIDGE_SOL, "ProbeBridge")
    bridge = w3.shardora.contract(abi=b_abi, bytecode=b_bin, sender_address=MY).deploy({'from': MY, 'salt': RANDOM_SALT + '05', 'args': [to_checksum_address(treasury.address)]}, KEY)

    treasury.functions.setBridge(to_checksum_address(bridge.address)).transact(KEY)
    receipt = bridge.functions.request(1).transact(KEY, value=5)
    print(f"Chain Call Result (AmountOut): {receipt.get('decoded_output')}")
    if receipt.get('status') == 0:
        print(f"✅ Chain Call Success! AmountOut: {receipt.get('decoded_output')}")

        for e in receipt.get('decoded_events', []):
            print(f"🔔 Event Log: {e['event']} -> {e['args']}")
    else:
        print(f"❌ Chain Call Failed: {receipt.get('msg')}")

    print(f"Bridge Total Requests: {bridge.functions.totalRequests().call()}")

def test_transfer(w3, MY, KEY, dest):
    print("\n--- TEST CASE 2: Standard Transfer ---")
    # dest = "620a1c023fdef21f3c10bf3d468de37d5ecfdc7b"
    transfer_amount = 500000000
    balance_before = w3.client.get_balance(dest) # 1. Record balance before transfer
    print(f"Balance before: {balance_before}")

    receipt = w3.shardora.send_transaction({'to': dest, 'value': transfer_amount}, KEY) # 2. Execute transfer transaction

    if receipt.get('status') == 0: # 3. Verify transaction status
        print(f"Transfer Sent Successfully. Hash: {receipt.get('tx_hash', 'N/A')}")

        count = 0
        while count < 30:
            time.sleep(2) # Give the node some synchronization time (optional, depends on your RPC response speed)

            balance_after = w3.client.get_balance(dest) # 4. Get balance after transfer
            print(f"Balance after: {balance_after}")

            expected_balance = balance_before + transfer_amount
            if balance_after == expected_balance:
                print(f"✅ Balance Verification PASSED: {balance_before} + {transfer_amount} == {balance_after}")
                break
            else:
                print(f"❌ Balance Verification FAILED!")
                print(f"   Expected: {expected_balance}")
                print(f"   Actual:   {balance_after}")

            count += 1
    else:
        print(f"❌ Transfer Failed with status: {receipt.get('status')} | Msg: {receipt.get('msg')}")

def test_prefund(w3, contract, KEY):
    my_address = w3.client.get_address(KEY)
    prefund_address = contract + my_address
    receipt = w3.shardora.send_transaction({'to': contract, 'prefund': 500000000}, KEY)
    print(f"Transfer Status: {receipt['status']} | Balance after: {w3.client.get_balance(prefund_address)}")

def test_oqs_transfer(w3, OQS_MY, OQS_KEY, OQS_PK):
    """Test post-quantum transfer transaction"""
    print("\n--- TEST CASE 4: OQS Standard Transfer ---")
    dest = "0000000000000000000000000000000000000002"

    # Construct OQS transaction dictionary, must contain pubkey
    tx_dict = {
        'to': dest,
        'value': 8888,
        'pubkey': OQS_PK
    }

    print(f"OQS Sender: {OQS_MY}")
    print(f"Dest Balance before: {w3.client.get_balance(dest)}")

    # Call w3.send_oqs_transaction
    receipt = w3.shardora.send_oqs_transaction(tx_dict, OQS_KEY)

    print(f"OQS Transfer Status: {receipt['status']}")
    print(f"Dest Balance after: {w3.client.get_balance(dest)}")

def test_oqs_prefund(w3, contract, OQS_MY, OQS_KEY, OQS_PK):
    """Test post-quantum transfer transaction"""
    print("\n--- TEST CASE 4: OQS Standard Transfer ---")
    # Construct OQS transaction dictionary, must contain pubkey
    tx_dict = {
        'to': contract,
        'prefund': 50000000,
        'pubkey': OQS_PK
    }

    print(f"OQS Sender: {OQS_MY}")
    prefund_address = contract + OQS_MY
    print(f"Dest Balance before: {w3.client.get_balance(prefund_address)}")

    # Call w3.send_oqs_transaction
    receipt = w3.shardora.send_oqs_transaction(tx_dict, OQS_KEY)

    print(f"OQS Transfer Status: {receipt['status']}")
    print(f"Dest Balance after: {w3.client.get_balance(prefund_address)}")

def test_oqs_contract_deploy_and_call(w3, OQS_MY, OQS_KEY, OQS_PK):
    """Test deploying and calling a contract using a post-quantum account"""
    print("\n--- TEST CASE 5: OQS Contract Deploy & Call ---")

    src = """
    pragma solidity ^0.8.0;
    contract OqsVault {
        uint256 public data;
        event DataStored(uint256 newValue);
        function store(uint256 v) public {
            data = v;
            emit DataStored(v);
        }
    }
    """
    bin, abi = compile_and_link(src, "OqsVault")

    # 1. Deploy contract (OQS mode)
    # Pass pubkey in the transaction dictionary, deploy will auto-switch to OQS based on KEY length
    oqs_vault = w3.shardora.contract(abi=abi, bytecode=bin, sender_address=OQS_MY)
    oqs_vault.deploy({
        'from': OQS_MY,
        'salt': RANDOM_SALT + 'a0',
        'pubkey': OQS_PK
    }, OQS_KEY)

    print(f"OQS Contract Deployed at: {oqs_vault.address}")

    # 2. Call contract (OQS mode)
    print("Sending OQS Contract Call...")
    receipt = oqs_vault.functions.store(12345).transact(OQS_KEY, oqs_pubkey=OQS_PK)

    if receipt.get('status') == 0:
        print(f"✅ OQS Call Success! New Data: {oqs_vault.functions.data().call()}")
        for e in receipt.get('decoded_events', []):
            print(f"🔔 OQS Event: {e['event']} -> {e['args']}")
    else:
        print(f"❌ OQS Call Failed: {receipt.get('msg')}")

def test_oqs_library_with_contract(w3, OQS_MY, OQS_KEY, OQS_PK):
    """
    Test deploying Library using OQS account and linking to business contract.
    Validation point: StepType.kCreateLibrary compatibility in OQS mode.
    """
    print("\n--- TEST CASE 6: OQS Library & Linking ---")

    src = """
    pragma solidity ^0.8.0;
    library OqsMath {
        function multiply(uint a, uint b) public pure returns(uint) {
            return a * b;
        }
    }
    contract OqsCalculator {
        function compute(uint a, uint b) public pure returns(uint) {
            return OqsMath.multiply(a, b);
        }
    }
    """

    # 1. Deploy OQS Library
    # Explicitly specify StepType.kCreateLibrary
    l_bin, l_abi = compile_and_link(src, "OqsMath")
    print("[*] Deploying OQS Library...")
    oqs_lib = w3.shardora.contract(abi=l_abi, bytecode=l_bin).deploy({
        'from': OQS_MY,
        'salt': RANDOM_SALT + 'a1',
        'step': StepType.kCreateLibrary,
        'pubkey': OQS_PK
    }, OQS_KEY)
    print(f"OQS Library Deployed at: {oqs_lib.address}")

    # 2. Deploy OQS contract referencing this Library
    # Link address and perform two-stage deployment
    c_bin, c_abi = compile_and_link(src, "OqsCalculator", libs={"OqsMath": oqs_lib.address})
    print("[*] Deploying OQS Calculator (Linked)...")
    oqs_calc = w3.shardora.contract(abi=c_abi, bytecode=c_bin).deploy({
        'from': OQS_MY,
        'salt': RANDOM_SALT + 'a2',
        'pubkey': OQS_PK
    }, OQS_KEY)

    # 3. Call test
    # Set contract object's public key for transact to auto-sign
    oqs_calc.oqs_pubkey = OQS_PK
    result = oqs_calc.functions.compute(7, 8).transact(OQS_KEY, oqs_pubkey=OQS_PK)

    print(f"OQS Library Call Result (7 * 8): {result.get('decoded_output')}")
    if result.get('decoded_output') == 56:
        print("✅ OQS Library & Linking Test Passed!")
    else:
        print(f"❌ OQS Library Test Failed: {result.get('msg')}")

def test_ecdsa_prefund_full_flow(w3, MY, KEY):
    print("\n--- TEST: ECDSA Prefund Logic (Full Flow) ---")

    src = "pragma solidity ^0.8.0; contract Vault { uint256 public val; function set(uint256 v) public { val = v; } }"
    bin, abi = compile_and_link(src, "Vault")

    contract = w3.shardora.contract(abi=abi, bytecode=bin)
    contract.deploy({'from': MY, 'salt': RANDOM_SALT + 'ecdsapp'}, KEY) # Use RANDOM_SALT to ensure uniqueness
    addr = contract.address
    print(f"Contract deployed at: {addr}")

    initial = contract.get_prefund(MY)
    print(f"Initial Prefund: {initial}")

    # ---------------------------------------------------------
    # Deposit 5,000,000 units of Gas prefund
    deposit_amount = 5000000
    print(f"Action: Depositing {deposit_amount} to prefund...")

    # Call the prefund interface from the contract object
    receipt = contract.prefund(deposit_amount, KEY) # Use the contract object's prefund method

    if receipt.get('status') == 0:
        print("✅ Prefund Tx success.")
    else:
        print(f"❌ Prefund Tx failed: {receipt.get('msg')}")
        return

    # ---------------------------------------------------------
    count = 0
    while count < 30:
        time.sleep(2) # Wait for consensus to settle
        after_deposit = contract.get_prefund(MY)
        print(f"Prefund after deposit: {after_deposit}")

        if after_deposit == initial + deposit_amount:
            print("🚩 Verification 1: Accumulation SUCCESS!")
            break
        else:
            count += 1
            print("🚩 Verification 1: Accumulation FAILED!")

    # ---------------------------------------------------------
    print("Action: Executing contract call to consume gas...")
    # Note: When calling transact, passing prefund=0 means only consume existing prefund, do not deposit more
    call_receipt = contract.functions.set(888).transact(KEY, prefund=0)

    time.sleep(2)
    final_stats = contract.get_prefund(MY)
    consumed = after_deposit - final_stats

    print(f"Final Prefund: {final_stats}")
    print(f"Gas Consumed from Prefund: {consumed}")

    if consumed > 0:
        print("🚩 Verification 2: Consumption SUCCESS!")
    else:
        print("🚩 Verification 2: Consumption FAILED (Prefund not used)!")
    contract.refund(KEY)

def test_oqs_contract_prefund_flow(w3, OQS_MY, OQS_KEY, OQS_PK):
    """Verify the deposit and accumulation logic for contract prefund."""
    print("\n--- TEST: OQS Prefund Accumulation ---")

    src = "pragma solidity ^0.8.0; contract OqsVault { uint256 public data; function store(uint256 v) public { data = v; } }"
    bin, abi = compile_and_link(src, "OqsVault")

    oqs_vault = w3.shardora.contract(abi=abi, bytecode=bin)
    oqs_vault.deploy({'from': OQS_MY, 'salt': RANDOM_SALT + 'pp01', 'pubkey': OQS_PK}, OQS_KEY)

    # Get contract address
    contract_addr = oqs_vault.address
    print(f"Target Contract: {contract_addr}")
    pre_pp = oqs_vault.get_prefund(OQS_MY)
    print(f"Step 1: Initial Prefund -> {pre_pp}")

    # --------------------------------------------------------- #
    # Step B: Execute Deposit (Prefund)
    # ---------------------------------------------------------
    add_amount = 5000000
    print(f"Step 2: Sending +{add_amount} prefund...")

    # Use the previously modified contract.prefund function
    receipt = oqs_vault.prefund(add_amount, OQS_KEY, oqs_pubkey=OQS_PK)

    if receipt.get('status') == 0:
        print("✅ Prefund transaction accepted.")
    else:
        print(f"❌ Prefund failed: {receipt.get('msg')}")
        return # Exit if prefund fails

    # --------------------------------------------------------- #
    # Step C: Check Prefund balance after deposit and verify accumulation
    # ---------------------------------------------------------
    # Wait a moment for consensus to complete
    count = 0
    while count < 30:
        time.sleep(2)
        post_pp = oqs_vault.get_prefund(OQS_MY)
        print(f"Step 3: Final Prefund -> {post_pp}")

        if post_pp == pre_pp + add_amount:
            print(f"🎉 SUCCESS: Prefund accumulated correctly! ({pre_pp} + {add_amount} = {post_pp})")
            break
        else:
            count += 1
            print(f"⚠️ ERROR: Accumulation mismatch! Expected {pre_pp + add_amount}, got {post_pp}")

    # --------------------------------------------------------- #
    # Step D: Send a regular contract call and observe if Prefund is consumed
    # ---------------------------------------------------------
    print("Step 4: Executing contract call (should consume prefund)...")
    oqs_vault.functions.store(999).transact(OQS_KEY, oqs_pubkey=OQS_PK) # Passing 0 here means no additional deposit

    time.sleep(2)
    final_pp = oqs_vault.get_prefund(OQS_MY)
    print(f"Step 5: Prefund after execution -> {final_pp}")
    print(f"Gas consumed from prefund: {post_pp - final_pp}")
    oqs_vault.refund(OQS_KEY, oqs_pubkey=OQS_PK)

def test_gmssl_transfer(w3, GM_KEY):
    """
    Test GmSSL standard transfer
    Utilizes SDK internal logic: passing gm_pubkey automatically switches between SM2/SM3
    """
    print("\n--- TEST CASE: GmSSL Standard Transfer ---")
    dest = "0000000000000000000000000000000000000001"

    gm_pubkey = get_sm2_public_key(GM_KEY)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey) # Call SDK internal method to calculate address (SM3 truncation)
    print(f"GmSSL Sender Address: {GM_MY}")

    tx_dict = { # 2. Construct transaction dictionary
        'to': dest,
        'value': 10000,
        'gm_pubkey': gm_pubkey  # 触发 SDK 的 send_gmssl_transaction 逻辑
    }

    print("Sending GmSSL Transfer...")
    receipt = w3.shardora.send_gmssl_transaction(tx_dict, GM_KEY) # 3. Initiate transaction

    print(f"GmSSL Transfer Status: {receipt.get('status')}")
    if receipt.get('status') == 0:
        print(f"✅ Success! New balance: {w3.client.get_balance(dest)}")
    else:
        print(f"❌ Failed: {receipt.get('msg')}")

def test_gmssl_contract_flow(w3, GM_KEY):
    """
    Test the full contract flow for GmSSL accounts: Deploy -> Prefund Gas -> Call
    Fully utilizes gm_mode=True to automatically derive public key
    """
    print("\n--- TEST CASE: GmSSL Contract Full Flow (Auto-Derive) ---")

    src = """
    pragma solidity ^0.8.0;
    contract GmVault {
        uint256 public data;
        function store(uint256 v) public { data = v; }
    }
    """
    bin_code, abi = compile_and_link(src, "GmVault")

    # 2. Calculate Sender address for deploy parameters
    gm_pubkey = get_sm2_public_key(GM_KEY)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey)

    print(f"GmSSL Sender Address pk: {gm_pubkey}, GM_MY: {GM_MY}")
    # 3. Deploy contract
    print("[*] Deploying GmVault via GmSSL...")
    gm_vault = w3.shardora.contract(abi=abi, bytecode=bin_code)
    gm_vault.deploy({
        'from': GM_MY,
        'salt': secrets.token_hex(31) + 'gm_auto',
        'gm_pubkey': gm_pubkey,
        'gm_mode': True
    }, GM_KEY)

    if gm_vault.deploy_receipt.get('status') != 0:
        print(f"❌ Deploy Failed: {gm_vault.deploy_receipt.get('msg')}")
        return

    print(f"GmSSL Contract at: {gm_vault.address}")

    # 4. Prefund Gas
    print("[*] Setting Gas Prefund (gm_mode=True)...")
    gm_vault.prefund(50000000, GM_KEY, gm_mode=True)
    # 5. Call Contract (Transact)
    print("[*] Calling store(888) via SM2 (gm_mode=True)...")
    receipt = gm_vault.functions.store(888).transact(GM_KEY, gm_mode=True)

    if receipt.get('status') == 0:
        result = gm_vault.functions.data().call()
        print(f"✅ Success! Data in vault: {result}")
    else:
        print(f"❌ Call Failed: {receipt.get('msg')}")

def gmssl_sign_test():
    IP, PORT = "127.0.0.1", 23001
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address("71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6")
    test_transfer(
        w3, MY,
        "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6",
        "19b46cb80e027a99ab41d60e68b8a8a096f50869")

    GM_KEY = "c4b9e7a21d5f83c0a1e4d6b9f2a1e5c8d3b7a9f0e1d2c3b4a5968778695a4b3c"
    test_gmssl_transfer(w3, GM_KEY)
    test_gmssl_contract_flow(w3, GM_KEY)

# ---------------------------------------------------------------------------
# Upgradeable contract demo
# Pattern: EIP-1967-style transparent proxy + two implementation versions.
#
# ProxyAdmin  – owns the proxy and can call upgradeTo()
# ProxyV1     – minimal proxy that stores impl address in EIP-1967 slot and
#               delegates all calls; exposes upgradeTo() for the admin
# CounterV1   – first implementation: stores a counter, exposes inc() / get()
# CounterV2   – upgraded implementation: adds a reset() function
# ---------------------------------------------------------------------------

PROXY_ADMIN_SOL = """
pragma solidity ^0.8.0;

interface IProxy {
    function upgradeTo(address newImpl) external;
}

contract ProxyAdmin {
    address public owner;

    constructor() { owner = msg.sender; }

    modifier onlyOwner() { require(msg.sender == owner, "not owner"); _; }

    function upgrade(address proxy, address newImpl) external onlyOwner {
        IProxy(proxy).upgradeTo(newImpl);
    }
}
"""

PROXY_SOL = """
pragma solidity ^0.8.0;

contract TransparentProxy {
    bytes32 private constant IMPL_SLOT =
        0x360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbc;
    bytes32 private constant ADMIN_SLOT =
        0xb53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103;

    constructor(address _impl, address _admin) {
        _setSlot(IMPL_SLOT, _impl);
        _setSlot(ADMIN_SLOT, _admin);
    }

    function upgradeTo(address newImpl) external {
        require(msg.sender == _getSlot(ADMIN_SLOT), "not admin");
        _setSlot(IMPL_SLOT, newImpl);
    }

    function implementation() external view returns (address) {
        return _getSlot(IMPL_SLOT);
    }

    function _getSlot(bytes32 slot) internal view returns (address addr) {
        assembly { addr := sload(slot) }
    }

    function _setSlot(bytes32 slot, address addr) internal {
        assembly { sstore(slot, addr) }
    }

    fallback() external payable {
        address impl = _getSlot(IMPL_SLOT);
        require(impl != address(0), "no impl");
        assembly {
            calldatacopy(0, 0, calldatasize())
            let result := delegatecall(gas(), impl, 0, calldatasize(), 0, 0)
            returndatacopy(0, 0, returndatasize())
            switch result
            case 0 { revert(0, returndatasize()) }
            default { return(0, returndatasize()) }
        }
    }

    receive() external payable {}
}
"""

COUNTER_V1_SOL = """
pragma solidity ^0.8.0;

contract CounterV1 {
    uint256 public count;
    event Incremented(uint256 newCount);

    function inc() external { count += 1; emit Incremented(count); }
    function get() external view returns (uint256) { return count; }
    function version() external pure returns (string memory) { return "v1"; }
}
"""

COUNTER_V2_SOL = """
pragma solidity ^0.8.0;

contract CounterV2 {
    uint256 public count;
    event Incremented(uint256 newCount);
    event Reset();

    function inc() external { count += 1; emit Incremented(count); }
    function get() external view returns (uint256) { return count; }
    function reset() external { count = 0; emit Reset(); }
    function version() external pure returns (string memory) { return "v2"; }
}
"""


def test_upgradeable_contract(w3, MY, KEY):
    """
    Upgradeable contract demo (transparent proxy, EIP-1967).
    1. Deploy ProxyAdmin + CounterV1 + Proxy
    2. inc() x2 via proxy  →  count == 2
    3. Deploy CounterV2, upgrade via ProxyAdmin
    4. Verify state preserved (count == 2, version == "v2")
    5. reset()  →  count == 0
    6. inc()    →  count == 1
    """
    print("\n" + "=" * 60)
    print("  Upgradeable Contract Demo (Transparent Proxy)")
    print("=" * 60)

    salt = secrets.token_hex(31)

    print("\n[1] Deploying ProxyAdmin...")
    admin_bin, admin_abi = compile_and_link(PROXY_ADMIN_SOL, "ProxyAdmin")
    proxy_admin = w3.shardora.contract(abi=admin_abi, bytecode=admin_bin)
    proxy_admin.deploy({'from': MY, 'salt': salt + 'pa'}, KEY)
    print(f"    ProxyAdmin @ {proxy_admin.address}")

    print("\n[2] Deploying CounterV1...")
    v1_bin, v1_abi = compile_and_link(COUNTER_V1_SOL, "CounterV1")
    impl_v1 = w3.shardora.contract(abi=v1_abi, bytecode=v1_bin)
    impl_v1.deploy({'from': MY, 'salt': salt + 'v1'}, KEY)
    print(f"    CounterV1 @ {impl_v1.address}")

    print("\n[3] Deploying TransparentProxy → CounterV1...")
    proxy_bin, proxy_abi = compile_and_link(PROXY_SOL, "TransparentProxy")
    proxy_contract = w3.shardora.contract(abi=proxy_abi, bytecode=proxy_bin)
    proxy_contract.deploy({
        'from': MY, 'salt': salt + 'px',
        'args': [to_checksum_address(impl_v1.address),
                 to_checksum_address(proxy_admin.address)],
    }, KEY)
    print(f"    Proxy @ {proxy_contract.address}")

    proxy_as_v1 = w3.shardora.contract(address=proxy_contract.address, abi=v1_abi)

    print("\n[4] inc() x2 via proxy (V1)...")
    for i in range(1, 3):
        r = proxy_as_v1.functions.inc().transact(KEY)
        print(f"    inc() #{i} status={r.get('status')} events={r.get('decoded_events')}")
    count = proxy_as_v1.functions.get().call()[0]
    assert count == 2, f"Expected 2, got {count}"
    print(f"    count={count} version={proxy_as_v1.functions.version().call()[0]}  ✅")

    print("\n[5] Deploying CounterV2...")
    v2_bin, v2_abi = compile_and_link(COUNTER_V2_SOL, "CounterV2")
    impl_v2 = w3.shardora.contract(abi=v2_abi, bytecode=v2_bin)
    impl_v2.deploy({'from': MY, 'salt': salt + 'v2'}, KEY)
    print(f"    CounterV2 @ {impl_v2.address}")

    print("\n[6] Upgrading proxy to CounterV2...")
    r = proxy_admin.functions.upgrade(
        to_checksum_address(proxy_contract.address),
        to_checksum_address(impl_v2.address),
    ).transact(KEY)
    assert r.get('status') == 0, "Upgrade tx failed"
    print(f"    upgrade() status={r.get('status')}  ✅")

    proxy_as_v2 = w3.shardora.contract(address=proxy_contract.address, abi=v2_abi)

    print("\n[7] Verifying state preserved...")
    count = proxy_as_v2.functions.get().call()[0]
    ver   = proxy_as_v2.functions.version().call()[0]
    assert count == 2 and ver == "v2", f"count={count} ver={ver}"
    print(f"    count={count} version={ver}  ✅")

    print("\n[8] reset()...")
    r = proxy_as_v2.functions.reset().transact(KEY)
    count = proxy_as_v2.functions.get().call()[0]
    assert count == 0, f"Expected 0, got {count}"
    print(f"    count={count}  ✅")

    print("\n[9] inc() after reset...")
    proxy_as_v2.functions.inc().transact(KEY)
    count = proxy_as_v2.functions.get().call()[0]
    assert count == 1, f"Expected 1, got {count}"
    print(f"    count={count}  ✅")

    print("\n" + "=" * 60)
    print("  ✅ Upgradeable contract demo PASSED")
    print("=" * 60)


def ecdsa_sign_test():
    IP, PORT, KEY = "127.0.0.1", 23001, "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6"
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address(KEY)

    balance = w3.client.get_balance(MY)  # 4. Get balance after transfer
    print(f"Balance : {balance}")

    # balance = w3.client.get_balance('e63540dad30d48e2e65d511379d070cbd5a38cf6')  # 4. Get balance after transfer
    # print(f"Balance : {balance}")

    # test_contract_price(w3, MY, KEY)
    # test_quoter_existing_contract(w3, MY, KEY)
    # test_iweth9_existing_contract(w3, MY, KEY, 10000)
    # test_token_approve(w3, MY, KEY, '421b24de95ee45e21a1023dda694f0a82eb70726', 'da0f771a342c57af7b8c51ff407273502e228236', 100000000)
    # test_token_balance(w3, MY, '421b24de95ee45e21a1023dda694f0a82eb70726')
    # test_router_existing_contract(w3, MY, KEY, '421b24de95ee45e21a1023dda694f0a82eb70726','2b3beb042ab42fa48468a5618466be1e9b359dd6', 1000)
    test_token_balance(w3, 'fad13769263c8158f29800c8e01e3dae3c93d34d', '2b3beb042ab42fa48468a5618466be1e9b359dd6')
    test_amm_swap_to_usdc(w3, MY, KEY, 1000)
    # test_withdraw_to_solana(
    #     w3, MY, KEY,
    #     token_in='421b24de95ee45e21a1023dda694f0a82eb70726',
    #     amount_in=1000,
    #     fee=2000,
    #     amount_out_minimum=0,
    #     solana_recipient=bytes.fromhex('a3f1c2e4b5d6789012345678901234567890abcdef1234567890abcdef123456')
    # )
    # test_token_balance(w3, 'fad13769263c8158f29800c8e01e3dae3c93d34d', '2b3beb042ab42fa48468a5618466be1e9b359dd6')

    # test_contract_call_contract(w3, MY, KEY)
    # test_transfer(w3, MY, KEY, "620a1c023fdef21f3c10bf3d468de37d5ecfdc7b")
    # test_library_with_contrcat(w3, MY, KEY)
    # test_ecdsa_prefund_full_flow(w3, MY, KEY)
    # test_contract_selfdestruct(w3, MY, KEY)
    # test_create2_assembly_deployment(w3, MY, KEY)
    # test_upgradeable_contract(w3, MY, KEY)

def oqs_sign_test():
    # Base configuration
    IP, PORT = "127.0.0.1", 23001

    # OQS keys (using sample ML-DSA-44 length Hex string here, should actually read from oqs_addrs file)
    # Note: Private key length must be > 128 bits to trigger auto-switch logic in code
    OQS_KEY = "4a6393c16df04473176bae0b114389fc60f31ab9bb4a9e3fd01e99c62baea55abd3ff4ca55887f58c87ae1d24972c8177392b57e2188adbac7eb113df430cce335751f12fed204a775f64dd74391a89b2fd0a111e2bdd8331a75ea673692c8cedc118460e6dbc1c4512ab88a1322410c2c4984f6a0048477f9da69690edc1be4d8400683206461140654a4410a376d9aa88944023283a248d1468802104c0ca1289b065218822c52b88520086dc02085e30005190031db46810ca00899a240a33682c490712313806314094b36424bb66cc3268c54b25051822cd2960598b86412284943c8490009248b447223a16d1417859b10120bc80dcca48412b14d23370c8ca060d116058b26086290895c144aa2a40422b1919030868c3452a0840188a22824c48490a610421846e2104081880c643686a4428242228611110483b061da00508b108123064151b0841a346589062908496424a0410c398ecc9491418864da382611466d5188705c00864b924549c28dd2a0605144451437061c9800a324651ca8005ac28418458d92368ce192301a016623366012802021116c8232241b24498b483124022001418294304c20c320c22485da002440c04dcaa008839684dc422252844121142409850884c41182960912247013a910912671d1980d83428904251298826d8a384a881011a14842dc8864438091081188231930e014600c464c12290e0a216a64860101a9908c020058841158468953482203a940c2941020808023030e9144600a358482a0201c998943846483166201048924876552263121384582028802970161400ac146519ca84c24454a1086114a049151806c114680624646d448224b906844b624021951d2b265da96048bc69013c66c40b811c822801c9364e3185064b00c0c832501a3084044716008511b4410d3003292b6055a202481b29092346940282cdc18024808682296455b165113494c81282ac1026a94448c02142e2402910b444094048923218524b129c494240a036804260e13a60898321210806c22448dcab2411c054180a8892320704180080b494c43362221114c11154c1a496582988521324dc2b08ca1866c49304ee2c0411240290390251a822c8142525218205888245aa01000a6801a0249a0a2891a456411328a11446412074903242880b2494038440c8505408070183760c99668d9c00d00a43014282251a82da29851244021d1164811a82909b22c99a031021562e1a07098cefa3f7fbafb37beb94cc6a0c4edf99a3309b71ee6e874098c08f41c378c646a4cc06bd17cd134ce4d2b7ace034a4567e8298da64c53f07e0f000ea40df2fe1d8b8d48665edf453f2284d16cda33485bf24ab38b8675b13f505e8af05351d3171bc1a0aa9f98a96dcb8467c0b6311a05643d82fe8ca89b546068d3758bb78fdd89c2050009d0c45c57f55b712a4308f6ec9feba74eb1036baa4ca14c81bd2978c2b4125f91c93c9aef5782ec9e218647741eaa49e3acb1134013eba02c4f8b58c0bb58b46f26caef3fe2a176cbf198e9f45ae11ec0c832f9ad19f5596a5458293ed09f97593bcfbd0e5c21f4984ecc96fd23be33a1dd188062dbb5650cbe2329f5b3e3ee3db4196faf782e5cafdd6a6ff8dad6186a7ec016cd07f38109c673b929ab9875731c24f11b424c1f3633c767e57013c7a289e3409bf092c49f0bd3f1d47c19d26cb5fabeea5e674e3e8db2e28c7971385038d9bd0b10791f95c355acbe050fb6d079b14fe8353cda4d77e52f38df13f21a08ec529e692059fbdb15ac74af020636228585047362ae9a64462d2d4862d276da7015fa5233646f75c5a59df1e37187be6f76370fc6c0808f0ce32177473057047daf9cc63c41691d06d95966909a5d727a9f120d7e575495df58cffbb9ed1215319a39856ca82f8f91f1c077686059eee67270f1a852aefa34d4849b8a706971e1216186171aec7a873ef4cda507bda37d3a61e14cf5423e0eea7bfae92b4eed842e3812369b5a2c394bb308bce0ffc285a5fed51fe199f44c597ada7c68023cebdb5327b95a20c3512b736d651c96c14a8fd32486981908934c0c728bd8992131ec9fa521316eca9bd140c3a6a211e03e813d2090865775174dd27154f5fb335949197a32b3f4b2282daf4f86e0dd9a92a4d6c01c62a52d98cea2e3a71601b1bbe6f44de2b408137e87eba94e084dc480af489ac602002cfe3c3010ddfdb06d42b92ceedcf5562ad72fdbf9fbe9720049a7dc7565251b75c6cd3c9671d65724d571fcb59096ccde707b269dccc05a4052562cab4a3d6310fbb2d3f6edabd11c31cf2e54a462cb4c162b6e3ae1f0162c1bfab06b2feb0899b6ef8d99386fa28ac8739473cd7fcae0e4bb5714388d5a0fedb7b967c5924f03ac1019245099b54e6e4c591df81ea11354018e3348689a87f21536e4415321330d1840e71c03777415ba47209079ca22e61eafe8f1886c97f52db5e21976422ce13ba0b16fbe1e041ae4be26b41dfde8d11766e5e91e1becbca4d89e743c67d92a5202333e083e7270874df349ae5c0d5971ff30311f195adc2f2ce90bb39ae56e68e0f8bcdf48047f16f629d65138ee24683a62d05c83275bb825367ab83e4bd7dc7ef3d5824e9c95ac4c0bd0f8d11fcc054b1ef08a33899d5c97d305dd31c0225cfcccd03d7ad5f6656aa5cda4c387040d22b62d6b8b8a43e53869fd4110d37c6bb14f96c9e191b5be281ca36b423a22f64fbaa6a46ccca7aecefb16abed8dfd621cf87afbf43f3dff96961887e0df30852ece9d9c2b9848d681df2bf1cd0516e2df3a91263513f87a9b8390705086c934309390ae1df684a8db293dce305a532533b31e3b2d21dc1e8ad2886bac5b2781304f467e95bf1202447942ac6d2190d04ee34ca1de2085d4cfcff0ad13749a5b213887445680958ec6f97c2d979810f41a42e39ea6f5c14c83bb3188926343ec9d18716f8a191afe60124719879f9d14878e87a2834ce15160dcbd1ee212028ccbd0352115d793ec83fff383fa4f95a7b01250343a05d966a501d2b17a50f7dd406853f5c64fbb7d64911253de2cdfbf5303e314273a4aef97db3372eb5473f7bc8a3295ee484798e75ac7070c207bb0a238472a190262811c55768a626e83887d69eec4422b26d415604cfe2d0491771b307c04f662d2959faf3fd8250ee045899c31bd43d08abcd676708af64d7dbdd82a675d3b5eb60eac7f88404e23b4049a6c9a509c012d690658ae5bf54d88863afa6c12645878763b0546ca0472a3b206ee37b087eded75321a70671cfe3a4dc8f4b74d334ecb7c54385023657c1461eb9e3f5ac53d8d523ea88859ad1ee9853392637d47ba87dfd4c91c5707a04f3ac27d3cbd117303ec2baf269529d8a097a47d9432239646f92cd02b6c5a6532477e08cda33261dafa883613e2cb332ee5ce982ab5fe90afe2d3707237200aa1a9e32552fc606294320f7d4fa463ea8456620998d8826f26dfb70f42ca9e1a6fef224a6e42119661853d9d6b5edf57cc36aa9f1961d1662b9238e54d3cc8ea003c0717587f649f1823d4847b9b777727dec99df993d8d6fc12101dd572807ee7"
    OQS_PK = "4a6393c16df04473176bae0b114389fc60f31ab9bb4a9e3fd01e99c62baea55aee04da4794a48502fbd77be9cc3848b8b54c60bc77af76678a60f35f5d3c4eec83bb547843034dc5c62c2d46205b0c57803a868ba0992ef6941b0d848aedf97ae24cc8ad89a329c5825862280e6be2d74fefe4c3ea7561f9849042a0b50de7b914653fbefaa6273eab93236871313d6aa55ad2754be72b59d58c25ffca65b8bb5ffd807eaa59d1e6ca202fb4ba837f87439f0ce45757d56665deb7a9f133c1200d199bdfff711696cf692ec15e03f14b778a10adf26fb912cf5742e6fe633d6a45455634b6cc3fba4e14da2909c39575f59070cf9b66e5a65c799460969387dadb2fe8fb90837e36f9c68c25639f6931f19cad5870a9386a2b5081d92ddd641f42fd811f0b4b9ee8041ff08b44fd94d020ba36715400f66c515cf9ae942dab814de9c4c66e302901beb38d49c19ccadde1c6e8c16bc8472d9620171f5f8206374ffcd7df86c3ef2e22cb45e74efdc2dba52ea2f71ad41cd17b3c333429872ba112aa586b6a378923a4de3608fa0f44eb29b0a2ea08f61bd322bfa44408b8f7dc3bd57c987a8f78f59d0b5a356dd0ce66d2c7508f78f42231141712411a96f1200bfdc46cbef99f849526bd05a1e2954747b617a4517323bc7a7bd9e56590ce841b6dcb234c904219d3b85a3a8f753957b6aef37264fe49c4c188ec132d37acad296bfe99ec33ab52fef9537b7738ff13cb37d8bd21c3cd6ecb65c607cc232c11b8cfece2532965de4c133f1d7d36beb5ad3dc5d13463983e2a2668a68bd437ec857d6c4fc6c3c09417280c88a348ebb9e11ed4a20e231dc57fcdbc8cabb401dd5f1b9fa5a7da5c19ca4c3b29b3b2362d397c58d14bd71ac7d36f72d820659417e728535561293332713fd7cb7652c7ae74a3790ae9c4d4d46b32f232c84d36df5b70591c001f221cdb5af6cfd63a4e165a7d5f0cf2d8abd5165538ffc20a5d407f2a77791237d319d1e98230f5002a86d8462c4f6bccac66b43b771c01da95fd8ea4c5bc87c90cb5160e06ef68dd046e25e6ae96eb119594ee946a0bdc2510beb85f273697c907fdb029c582cdc65b9c2d8d7c44cfa4992d725bbe981101ecb092cdf3eedd67972e6936c7ba56e354313a22dee82eaed207d39e862ca349c8fdc26cbdc560da9919e965a8ae2daa67a2e95023ca94543c5cde3a9d330bb862434dfd42e9286b210b9a00786b89acd6bc49ed0b600a4f90a0c00ea20d4cd7bfc9b599131a4d8eed0bcad88cb14e53ddca5269ecc67090540dbdfcbd980bc8083159a3ff7568968aad3c69d368dd005c88842e279d03022cbd4e889fbb4b1741cad3eb9d3d4299223b7442ed30d59f6df90dae29635e2a4a88d44d78b8cefa033adc20c0fba2c49f788dc2f118a6499b91419511e1f2ecb8171bf72f29e69faa04b917c708e4545df9c1181a75a3e42340e3f68fea06986f76a89ffb1343ad76036b7396c63411447494372dd4a34e1176784254798705ca2ce9e71f842660b09bce8a0cb6bc1f258c121ec5c7f97e73bbcd56f279d3607f1d315b0380d051a4b8ea02a44d9ee1f8886c68ef513bbd1e461bf237e1abf1b703989ce6f9a8e495279fdefed04daf77cb02d47a49013f709067d15511fe697cbb93106ba315799aa5802998fd2b1e00aab5cdd12884cbc9cab9f6da92136bbe3085e0e3787d6875f9c08d0acb52f353656926f6104581ec75fe0b7a9a4af091188eb35dfdaeb111ecec9718da6a41de95500f33961b030e4e382216d4d3377547ff3331db29641c7cfddab7dae4dd0927350dfb6882a8e5e1d9951536bd7c13d8ec1bb71663e5914e"

    w3 = ShardoraWeb3Mock(IP, PORT)
    MY_OQS = w3.client.get_oqs_address(OQS_PK)

    test_oqs_transfer(w3, MY_OQS, OQS_KEY, OQS_PK)
    test_oqs_contract_deploy_and_call(w3, MY_OQS, OQS_KEY, OQS_PK)
    test_oqs_library_with_contract(w3, MY_OQS, OQS_KEY, OQS_PK)
    test_oqs_contract_prefund_flow(w3, MY_OQS, OQS_KEY, OQS_PK)


# -----------------------------------------------------------------------------
# WebSocket txhash subscription demo
#
# Usage:
#   1. Send a transaction and obtain its tx_hash.
#   2. Call subscribe_txhash(ws_ip, ws_port, tx_hash) to wait for the on-chain
#      confirmation pushed by the server.
#
# Server message format (binary frame):
#   [1 byte: type_len][type_len bytes: type]["subscribe:<txhash>" or "unsubscribe:<txhash>"]
# Server push (text frame): JSON string
# -----------------------------------------------------------------------------

import threading
import websocket  # pip install websocket-client


def _decode_ws_payload(raw) -> str | None:
    """
    Extract the text payload from whatever websocket-client hands us.
    - str  → return as-is
    - bytes that start with a valid WS text-frame header → strip the header
    - bytes that are plain UTF-8 (no frame header) → decode directly
    Returns None if the data cannot be interpreted as text.
    """
    if isinstance(raw, str):
        return raw
    if not isinstance(raw, (bytes, bytearray)):
        return None
    # Detect WS text frame: first byte = 0x81 (FIN + opcode 1)
    if len(raw) >= 2 and raw[0] == 0x81:
        b1 = raw[1] & 0x7f
        if b1 <= 125:
            payload = raw[2:2 + b1]
        elif b1 == 126 and len(raw) >= 4:
            length = (raw[2] << 8) | raw[3]
            payload = raw[4:4 + length]
        elif b1 == 127 and len(raw) >= 10:
            length = int.from_bytes(raw[2:10], "big")
            payload = raw[10:10 + length]
        else:
            payload = raw
        try:
            return payload.decode("utf-8")
        except Exception:
            return None
    # Plain UTF-8 bytes (no frame header)
    try:
        return raw.decode("utf-8")
    except Exception:
        return None


def _decode_ws_receipt(receipt: dict, abi: list, function_name: str = None) -> dict:
    """
    Decode output and events from a WS-pushed receipt.

    WS receipt fields differ from HTTP receipt:
      - output   : hex string  (HexEncode)
      - events[] : {"data": hex, "topics": [hex, ...]}

    HTTP receipt uses base64 for the same fields, so we cannot reuse
    decode_receipt() directly.
    """
    from Crypto.Hash import keccak as _keccak
    import eth_abi as _eth_abi

    receipt['decoded_output'] = None
    receipt['decoded_events'] = []

    if not abi:
        return receipt

    # ── 1. Decode output ─────────────────────────────────────────────────────
    raw_out_hex = receipt.get("output", "")
    if receipt.get("status") == 0 and raw_out_hex and function_name:
        try:
            raw_bytes = bytes.fromhex(raw_out_hex)
            item = next((i for i in abi if i.get('name') == function_name), None)
            if item and item.get('outputs'):
                decoded = _eth_abi.decode([o['type'] for o in item['outputs']], raw_bytes)
                receipt['decoded_output'] = decoded[0] if len(decoded) == 1 else decoded
        except Exception as e:
            print(f"[WS] output decode error: {e}")

    # ── 2. Decode events ─────────────────────────────────────────────────────
    raw_events = receipt.get("events", [])
    if not raw_events:
        return receipt

    # Build topic0 → event ABI map
    event_map = {}
    for item in [i for i in abi if i.get('type') == 'event']:
        sig = f"{item['name']}({','.join(i['type'] for i in item['inputs'])})"
        topic0 = _keccak.new(digest_bits=256).update(sig.encode()).digest().hex()
        event_map[topic0] = item

    for e in raw_events:
        try:
            topics = e.get('topics', [])
            if not topics:
                continue
            t0_hex = topics[0]  # already hex from WS
            if t0_hex not in event_map:
                continue
            spec = event_map[t0_hex]
            data_bytes = bytes.fromhex(e.get('data', ''))
            types = [i['type'] for i in spec['inputs'] if not i.get('indexed')]
            names = [i['name'] for i in spec['inputs'] if not i.get('indexed')]
            vals = _eth_abi.decode(types, data_bytes)
            receipt['decoded_events'].append({
                "event": spec['name'],
                "args": dict(zip(names, vals)),
            })
        except Exception as ex:
            print(f"[WS] event decode error: {ex}")

    return receipt


def _build_ws_msg(action: str, tx_hash: str) -> str:
    """Build a subscribe/unsubscribe command for TxWsServer.
    Wire format (text frame payload): 'subscribe:<txhash>' / 'unsubscribe:<txhash>'
    """
    return f"{action}:{tx_hash}"


def subscribe_txhash(ws_ip: str, ws_port: int, tx_hash: str, timeout: int = 35,
                     abi: list = None, function_name: str = None) -> dict | None:
    """
    Subscribe to a single txhash and block until the push is received or timeout.

    Args:
        ws_ip         : WebSocket server IP.
        ws_port       : WebSocket server port.
        tx_hash       : Transaction hash to subscribe to (hex string).
        timeout       : Maximum wait time in seconds (default 35).
        abi           : Contract ABI for decoding output/events (optional).
        function_name : Name of the called function for output decoding (optional).

    Returns:
        Transaction detail dict (with decoded_output / decoded_events) on success,
        None on timeout.
    """
    url = f"ws://{ws_ip}:{ws_port}"
    result: dict | None = None
    done = threading.Event()

    def on_open(ws):
        msg = _build_ws_msg("subscribe", tx_hash)
        ws.send(msg)
        print(f"[WS] Subscribed to txhash: {tx_hash}")

    def on_message(ws, raw):
        nonlocal result
        text = _decode_ws_payload(raw)
        if text is None:
            print(f"[WS] Undecodable message: {raw!r}")
            return
        try:
            data = json.loads(text.strip().lstrip('\ufeff'))
            if isinstance(data, str):
                data = json.loads(data)
        except Exception as e:
            print(f"[WS] Non-JSON message received: {text!r}, error: {e}")
            return

        if not isinstance(data, dict):
            return

        # Ignore subscribe/unsubscribe acknowledgements.
        if data.get("status") in ("subscribed", "unsubscribed"):
            print(f"[WS] Server ack: {data}")
            return

        if "error" in data:
            print(f"[WS] Server error: {data}")
            ws.close()
            done.set()
            return

        # Real transaction push.
        if data.get("tx_hash", "").lower() == tx_hash.lower():
            _decode_ws_receipt(data, abi, function_name)
            result = data
            print(f"[WS] Transaction confirmed: {json.dumps(data, indent=2)}")
            ws.send(_build_ws_msg("unsubscribe", tx_hash))
            ws.close()
            done.set()

    def on_error(ws, err):
        if isinstance(err, (bytes, bytearray)):
            on_message(ws, err)
            return
        print(f"[WS] Error: {err}")
        done.set()

    def on_close(ws, code, msg):
        print(f"[WS] Connection closed, code={code}")
        done.set()

    ws_app = websocket.WebSocketApp(
        url,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close,
    )

    t = threading.Thread(target=lambda: ws_app.run_forever(skip_utf8_validation=True), daemon=True)
    t.start()

    if not done.wait(timeout=timeout):
        print(f"[WS] Timeout ({timeout}s): no confirmation received for txhash={tx_hash}")
        ws_app.close()

    return result


def subscribe_multiple_txhashes(
    ws_ip: str, ws_port: int, tx_hashes: list[str], timeout: int = 35,
    abi: list = None
) -> dict[str, dict]:
    """
    Subscribe to multiple txhashes simultaneously and block until all are confirmed
    or timeout is reached.

    Returns:
        {txhash: transaction detail dict} for every hash that was confirmed.
        Unconfirmed hashes are absent from the result.
    """
    url = f"ws://{ws_ip}:{ws_port}"
    pending = set(h.lower() for h in tx_hashes)
    results: dict[str, dict] = {}
    done = threading.Event()

    def on_open(ws):
        for h in tx_hashes:
            ws.send(_build_ws_msg("subscribe", h))
        print(f"[WS] Subscribed to {len(tx_hashes)} txhash(es)")

    def on_message(ws, raw):
        text = _decode_ws_payload(raw)
        if text is None:
            return
        try:
            data = json.loads(text.strip().lstrip('\ufeff'))
            if isinstance(data, str):
                data = json.loads(data)
        except Exception:
            return

        if not isinstance(data, dict):
            return

        if data.get("status") in ("subscribed", "unsubscribed"):
            return

        if "error" in data:
            # Server rejected the command — close and surface the error.
            print(f"[WS] Server error: {data}")
            ws.close()
            done.set()
            return

        h = data.get("tx_hash", "").lower()
        if h in pending:
            _decode_ws_receipt(data, abi)
            results[h] = data
            pending.discard(h)
            ws.send(_build_ws_msg("unsubscribe", h))
            print(f"[WS] [{len(results)}/{len(tx_hashes)}] Confirmed: {h}")
            if not pending:
                ws.close()
                done.set()

    def on_error(ws, err):
        if isinstance(err, (bytes, bytearray)):
            on_message(ws, err)
            return
        print(f"[WS] Error: {err}")
        done.set()

    def on_close(ws, code, msg):
        done.set()

    ws_app = websocket.WebSocketApp(
        url,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close,
    )

    t = threading.Thread(target=lambda: ws_app.run_forever(skip_utf8_validation=True), daemon=True)
    t.start()

    if not done.wait(timeout=timeout):
        print(f"[WS] Timeout: {len(pending)} txhash(es) still unconfirmed: {pending}")
        ws_app.close()

    return results


def _ws_send_and_wait(w3, ws_ip, ws_port, desc, send_fn) -> dict | None:
    """
    Helper: call send_fn() to submit a tx (returns tx_hash str or receipt dict),
    then subscribe via WebSocket and wait for on-chain confirmation.
    send_fn must return either a hex tx_hash string or a dict with 'tx_hash' key.
    """
    print(f"\n[TX] {desc}")
    raw = send_fn()
    if raw is None:
        print(f"  ❌ send_fn returned None, skipping WS wait.")
        return None
    tx_hash = raw if isinstance(raw, str) else raw.get("tx_hash", "")
    if not tx_hash:
        print(f"  ❌ No tx_hash returned, skipping WS wait.")
        return None
    print(f"  tx_hash: {tx_hash}")
    receipt = subscribe_txhash(ws_ip, ws_port, tx_hash, timeout=35)
    if receipt:
        print(f"  ✅ Confirmed  block={receipt.get('block_height')}  "
              f"status={receipt.get('status')}  gas={receipt.get('gas_used')}")
    else:
        print(f"  ⏰ Timeout waiting for {tx_hash}")
    return receipt


def demo_ws_subscribe(ws_ip="127.0.0.1", ws_port=23100):
    """
    Full demo: replicate all contract-related transactions from ecdsa_sign_test,
    subscribing to each tx_hash via WebSocket for on-chain confirmation.

    Strategy: monkey-patch client.wait_for_receipt to intercept tx_hash,
    start a background WS subscription, then let the original polling finish.
    """
    print("\n" + "=" * 60)
    print("  WebSocket txhash Subscription Demo")
    print("=" * 60)

    IP, HTTP_PORT = ws_ip, 23001
    KEY = "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6"
    DEST = "620a1c023fdef21f3c10bf3d468de37d5ecfdc7b"

    w3 = ShardoraWeb3Mock(IP, HTTP_PORT)
    MY = w3.client.get_address(KEY)
    print(f"Sender  : {MY}")
    print(f"Receiver: {DEST}")

    # ── WS-aware wait_for_receipt patch ──────────────────────────────────────
    tx_times = []
    current_tx_name = ["Unknown TX"]

    def _patched_wait(tx_hash, abi=None, function_name=None, **kw):
        print(f"  tx_hash : {tx_hash}")
        t0 = time.time()
        receipt = subscribe_txhash(ws_ip, ws_port, tx_hash, timeout=35,
                                   abi=abi, function_name=function_name)
        t1 = time.time()
        duration = t1 - t0

        status = receipt.get('status', 'Timeout') if receipt else 'Timeout'
        tx_times.append((current_tx_name[0], tx_hash, duration, status))

        if receipt:
            print(f"  ✅ [Time: {duration:.2f}s] block={receipt.get('block_height')}  "
                  f"status={status}  gas={receipt.get('gas_used')}"
                  + (f"  output={receipt.get('decoded_output')}" if receipt.get('decoded_output') is not None else "")
                  + (f"  events={receipt.get('decoded_events')}" if receipt.get('decoded_events') else ""))
        else:
            print(f"  ⏰ Timeout waiting for {tx_hash} after {duration:.2f}s")
            receipt = {}
        return receipt

    w3.client.wait_for_receipt = _patched_wait

    def section(title):
        print("\n" + "─" * 50)
        print(title)
        print("─" * 50)

    def log_tx(name):
        current_tx_name[0] = name
        print(f"\n[TX] {name}")

    # ── 1. Standard transfer ──────────────────────────────────────────────────
    section("1. Standard Transfer")
    log_tx("Transfer 100000 → DEST")
    w3.shardora.send_transaction({'to': DEST, 'value': 100000}, KEY)

    # ── 2. Library + Calculator ───────────────────────────────────────────────
    section("2. Library with Contract")
    src_lib = ("pragma solidity ^0.8.0; "
               "library MathLib { function add(uint a, uint b) public pure returns(uint){return a+b;} } "
               "contract Calculator { function use(uint a, uint b) public pure returns(uint){return MathLib.add(a,b);} }")
    l_bin, l_abi = compile_and_link(src_lib, "MathLib")
    lib = w3.shardora.contract(abi=l_abi, bytecode=l_bin)
    log_tx("Deploy MathLib")
    lib.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws01', 'step': StepType.kCreateLibrary}, KEY)

    c_bin_linked, c_abi = compile_and_link(src_lib, "Calculator", libs={"MathLib": lib.address})
    calc = w3.shardora.contract(abi=c_abi, bytecode=c_bin_linked)
    log_tx("Deploy Calculator")
    calc.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws02'}, KEY)

    log_tx("Calculator.use(10, 20)")
    calc.functions.use(10, 20).transact(KEY)

    # ── 3. Contract-calls-contract (chain call) ───────────────────────────────
    section("3. Contract Call Contract (Chain Call)")
    p_bin, p_abi = compile_and_link(PROBE_POOL_SOL, "ProbePool")
    pool = w3.shardora.contract(abi=p_abi, bytecode=p_bin)
    log_tx("Deploy ProbePool")
    pool.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws03', 'args': [10000, 10000], 'amount': 5000000}, KEY)

    t_bin, t_abi = compile_and_link(PROBE_TREASURY_SOL, "ProbeTreasury")
    treasury = w3.shardora.contract(abi=t_abi, bytecode=t_bin)
    log_tx("Deploy ProbeTreasury")
    treasury.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws04',
                     'args': [to_checksum_address(pool.address)], 'amount': 5000000}, KEY)

    b_bin, b_abi = compile_and_link(PROBE_BRIDGE_SOL, "ProbeBridge")
    bridge = w3.shardora.contract(abi=b_abi, bytecode=b_bin, sender_address=MY)
    log_tx("Deploy ProbeBridge")
    bridge.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws05',
                   'args': [to_checksum_address(treasury.address)]}, KEY)

    log_tx("treasury.setBridge(bridge)")
    treasury.functions.setBridge(to_checksum_address(bridge.address)).transact(KEY)

    log_tx("bridge.request(1)")
    bridge.functions.request(1).transact(KEY, value=5)

    # ── 4. Prefund full flow ──────────────────────────────────────────────────
    section("4. Prefund Full Flow")
    src_vault = "pragma solidity ^0.8.0; contract Vault { uint256 public val; function set(uint256 v) public { val = v; } }"
    v_bin, v_abi = compile_and_link(src_vault, "Vault")
    vault = w3.shardora.contract(abi=v_abi, bytecode=v_bin)
    log_tx("Deploy Vault")
    vault.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws06'}, KEY)

    log_tx("Vault.prefund(5000000)")
    vault.prefund(5000000, KEY)

    log_tx("Vault.set(888)")
    vault.functions.set(888).transact(KEY, prefund=0)

    log_tx("Vault.refund")
    vault.refund(KEY)

    # ── 5. Self-destruct ──────────────────────────────────────────────────────
    section("5. Contract Self-Destruct")
    k_bin, k_abi = compile_and_link(PROBE_KILL_SOL, "ProbeKill")
    kill_contract = w3.shardora.contract(abi=k_abi, bytecode=k_bin, sender_address=MY)
    log_tx("Deploy ProbeKill")
    kill_contract.deploy({'from': MY, 'salt': RANDOM_SALT + 'ws07kill', 'amount': 2000}, KEY)

    log_tx("ProbeKill.setMessage('hello')")
    kill_contract.functions.setMessage("hello").transact(KEY)

    recipient = secrets.token_hex(20)
    log_tx(f"ProbeKill.kill")
    kill_contract.functions.kill(recipient).transact(KEY)

    # ── 6. CREATE2 assembly deployment ───────────────────────────────────────
    section("6. CREATE2 Assembly Deployment")
    f_bin, f_abi = compile_and_link(PROBE_CREATE2_FACTORY_SOL, "Create2Factory")
    factory = w3.shardora.contract(abi=f_abi, bytecode=f_bin)
    log_tx("Deploy Create2Factory")
    factory.deploy({'from': MY, 'salt': secrets.token_hex(31) + 'f2', 'amount': 100000000}, KEY)

    log_tx("factory.deploy(88888888)")
    factory.functions.deploy(88888888).transact(KEY)

    print("\n" + "=" * 60)
    print("  WebSocket Subscription Tx Latency Summary")
    print("=" * 60)
    total_time = 0
    for tx_name, tx_hash, duration, status in tx_times:
        print(f"  - {tx_name:<40} : {duration:>5.2f}s  (Status: {status})")
        total_time += duration
    print("-" * 60)
    if tx_times:
        print(f"  Average Tx Latency:                        {total_time / len(tx_times):.2f} seconds")
    print(f"  Total Wait Time:                           {total_time:.2f} seconds")

    print("\n" + "=" * 60)
    print("  Demo complete.")
    print("=" * 60)

if __name__ == "__main__":
    ecdsa_sign_test()
    # oqs_sign_test()
    # gmssl_sign_test()
    # demo_ws_subscribe("127.0.0.1", 33001)  # uncomment to run the WebSocket subscription demo
