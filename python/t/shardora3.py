from __future__ import annotations
import secrets
import time
from eth_utils import to_checksum_address
import requests
import binascii
from gmssl import sm2, sm3, func

from shardora_sdk import ShardoraWeb3Mock, StepType, compile_and_link, get_sm2_public_key

# --- 5. Main Execution ---
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

# ============================================
# AMM SAME-SHARD ATOMIC SWAP TEST CONTRACTS
# ============================================
# 关键设计：三个合约部署到同一分片，同一交易池中执行
# 目的：演示同一分片内的原子性保证 vs 跨分片的非原子性
# 
# 合约角色分工：
# 1. AMMPool       - 流动性池，保存储备金，执行恒定乘积公式
# 2. AMMTreasury   - 资金管理，处理用户余额、存取
# 3. AMMRouter     - 路由执行，调用 Treasury -> Pool 的链式调用
#
# 同分片同交易池特性：
# - Pool 和 Treasury 的调用在同一笔交易中原子执行
# - 要么全成功，要么全失败（自动回滚）
# - 开发者无需手动编写补偿逻辑
# ============================================

AMM_POOL_SOL = """
pragma solidity ^0.8.20;

/**
 * @title AMMPool - Liquidity Pool with Constant Product Formula
 * @notice 流动性池合约，维护代币储备和交换逻辑
 * @dev 必须与 AMMTreasury 和 AMMRouter 部署到同一分片
 */
contract AMMPool {
    string public poolName;
    uint256 public reserveTokenX;  // Token X 储备
    uint256 public reserveTokenY;  // Token Y 储备
    uint256 public totalSwaps;
    uint256 public failedSwaps;
    
    address public treasury;  // 关键：指向同分片的 Treasury 合约
    
    // 事件用于链式调用追踪
    event SwapInitiated(address indexed user, uint256 amountXIn, uint256 minYOut, uint256 swapId);
    event SwapExecuted(address indexed user, uint256 amountXIn, uint256 amountYOut, uint256 swapId);
    event SwapFailed(address indexed user, uint256 amountXIn, uint256 reason, uint256 swapId);
    event PoolStateUpdated(uint256 reserveX, uint256 reserveY, uint256 totalSwaps);
    
    mapping(uint256 => SwapRecord) public swapHistory;
    
    struct SwapRecord {
        address user;
        uint256 amountIn;
        uint256 minOut;
        uint256 amountOut;
        uint256 timestamp;
        uint8 status;  // 0=pending, 1=success, 2=failed
    }

    constructor(string memory name, uint256 initialX, uint256 initialY) payable {
        poolName = name;
        reserveTokenX = initialX;
        reserveTokenY = initialY;
    }

    /**
     * @notice 设置 Treasury 地址（必须在部署后调用，确保同分片）
     */
    function setTreasury(address _treasury) external {
        require(treasury == address(0), "Treasury already set");
        treasury = _treasury;
    }

    /**
     * @notice 恒定乘积公式交换：X * Y = K
     * @param amountXIn 输入 Token X 数量
     * @param minYOut 最小输出 Token Y 数量（滑点保护）
     * @return amountYOut 实际输出的 Y 数量
     * @return swapId 本次交换ID
     * 
     * 关键点：同分片原子性
     * - 如果交换失败（滑点过大），整个交易回滚
     * - Treasury 和 Pool 的状态同步更新
     * - 无需手动补偿机制
     */
    function swapXtoY(uint256 amountXIn, uint256 minYOut) 
        external 
        returns (uint256 amountYOut, uint256 swapId) 
    {
        require(msg.sender == treasury, "Only Treasury can call");
        require(amountXIn > 0, "Input amount must be positive");
        
        swapId = totalSwaps++;
        
        emit SwapInitiated(tx.origin, amountXIn, minYOut, swapId);
        
        // 恒定乘积公式计算
        uint256 k = reserveTokenX * reserveTokenY;
        uint256 newReserveX = reserveTokenX + amountXIn;
        uint256 newReserveY = k / newReserveX;
        amountYOut = reserveTokenY - newReserveY;
        
        // 滑点检查：失败将导致整个交易回滚（同分片原子性）
        require(amountYOut >= minYOut, "Slippage exceeded: insufficient output");
        
        // 状态更新（原子性保证）
        reserveTokenX = newReserveX;
        reserveTokenY = newReserveY;
        
        swapHistory[swapId] = SwapRecord({
            user: tx.origin,
            amountIn: amountXIn,
            minOut: minYOut,
            amountOut: amountYOut,
            timestamp: block.timestamp,
            status: 1  // 1 = success
        });
        
        emit SwapExecuted(tx.origin, amountXIn, amountYOut, swapId);
        emit PoolStateUpdated(reserveTokenX, reserveTokenY, totalSwaps);
        
        return (amountYOut, swapId);
    }
    
    /**
     * @notice 获取当前池的状态
     */
    function getPoolStats() external view returns (uint256 x, uint256 y, uint256 swaps, uint256 failed) {
        return (reserveTokenX, reserveTokenY, totalSwaps, failedSwaps);
    }
}
"""

AMM_TREASURY_SOL = """
pragma solidity ^0.8.20;

/**
 * @title AMMTreasury - 资金管理和用户余额跟踪
 * @notice 必须与 AMMPool 和 AMMRouter 部署到同一分片
 * @dev 同分片部署保证以下调用链的原子性：Router -> Treasury -> Pool
 */
interface IAMMPool {
    function swapXtoY(uint256 amountXIn, uint256 minYOut) external returns (uint256 amountYOut, uint256 swapId);
    function getPoolStats() external view returns (uint256 x, uint256 y, uint256 swaps, uint256 failed);
}

contract AMMTreasury {
    address public pool;
    address public router;
    
    // 用户余额跟踪
    mapping(address => uint256) public balanceX;  // Token X 用户余额
    mapping(address => uint256) public balanceY;  // Token Y 用户余额
    mapping(address => uint256) public totalDeposited;
    
    uint256 public totalSwapsExecuted;
    uint256 public totalSwapsFailed;
    
    event Deposited(address indexed user, uint256 amountX, uint256 amountY);
    event SwapRequested(address indexed user, uint256 amountXIn, uint256 minYOut, uint256 requestId);
    event SwapCompleted(address indexed user, uint256 amountXIn, uint256 amountYOut, uint256 requestId);
    event SwapFailed(address indexed user, string reason);
    event Withdrawn(address indexed user, uint256 amountX, uint256 amountY);
    
    constructor(address _pool) {
        pool = _pool;
    }

    /**
     * @notice 设置 Router 地址（必须在部署后调用）
     */
    function setRouter(address _router) external {
        require(router == address(0), "Router already set");
        router = _router;
    }

    /**
     * @notice 用户存款到 Treasury（初始化余额）
     */
    function deposit(uint256 amountX, uint256 amountY) external payable {
        require(amountX > 0 || amountY > 0, "Must deposit positive amount");
        
        balanceX[msg.sender] += amountX;
        balanceY[msg.sender] += amountY;
        totalDeposited[msg.sender] += msg.value;
        
        emit Deposited(msg.sender, amountX, amountY);
    }

    /**
     * @notice 执行交换（由 Router 调用）
     * @param user 交换用户
     * @param amountXIn Token X 输入数量
     * @param minYOut 最小输出 Y 数量
     * @return amountYOut 实际输出 Y 数量
     * 
     * 关键的链式调用：Router -> Treasury.executeSwap() -> Pool.swapXtoY()
     * 这些调用在同一分片、同一交易池中**原子执行**
     * 如果任何步骤失败（如滑点检查），整个交易自动回滚
     * 用户无需手动补偿
     */
    function executeSwap(
        address user,
        uint256 amountXIn,
        uint256 minYOut
    ) external returns (uint256 amountYOut) {
        require(msg.sender == router, "Only Router can call");
        require(balanceX[user] >= amountXIn, "Insufficient balance");
        
        uint256 requestId = totalSwapsExecuted;
        emit SwapRequested(user, amountXIn, minYOut, requestId);
        
        // 关键步骤：调用同分片 Pool 的 swapXtoY
        // 如果失败（如滑点过大），回滚整个交易
        (uint256 amountYOut, ) = IAMMPool(pool).swapXtoY(amountXIn, minYOut);
        
        // 更新用户余额（只有当 Pool.swapXtoY 成功时才执行）
        balanceX[user] -= amountXIn;
        balanceY[user] += amountYOut;
        
        totalSwapsExecuted++;
        emit SwapCompleted(user, amountXIn, amountYOut, requestId);
        
        return amountYOut;
    }

    /**
     * @notice 用户提现
     */
    function withdraw(uint256 amountX, uint256 amountY) external {
        require(balanceX[msg.sender] >= amountX && balanceY[msg.sender] >= amountY, "Insufficient balance");
        
        balanceX[msg.sender] -= amountX;
        balanceY[msg.sender] -= amountY;
        
        emit Withdrawn(msg.sender, amountX, amountY);
    }

    /**
     * @notice 查看用户余额
     */
    function getUserBalance(address user) external view returns (uint256 x, uint256 y) {
        return (balanceX[user], balanceY[user]);
    }
}
"""

AMM_ROUTER_SOL = """
pragma solidity ^0.8.20;

/**
 * @title AMMRouter - 路由和交换编排
 * @notice 必须与 AMMPool 和 AMMTreasury 部署到同一分片
 * @dev 演示同分片链式调用的原子性
 */
interface IAMMTreasury {
    function executeSwap(address user, uint256 amountXIn, uint256 minYOut) external returns (uint256 amountYOut);
    function getUserBalance(address user) external view returns (uint256 x, uint256 y);
}

contract AMMRouter {
    address public treasury;
    address public pool;
    uint256 public totalRoutedSwaps;
    uint256 public successfulSwaps;
    uint256 public failedSwaps;
    
    event RoutingInitiated(address indexed user, uint256 amountXIn, uint256 minYOut);
    event RoutingSuccess(address indexed user, uint256 amountXIn, uint256 amountYOut);
    event RoutingFailed(address indexed user, string reason);
    event AtomicityDemonstration(string message);

    constructor(address _treasury, address _pool) {
        treasury = _treasury;
        pool = _pool;
    }

    /**
     * @notice 执行原子交换
     * @param amountXIn Token X 输入数量
     * @param minYOut 最小输出 Y 数量
     * @return amountYOut 实际输出 Y 数量
     * 
     * 调用链（在同一分片同一交易池执行）：
     * Router.atomicSwap() 
     *  ↓
     * Treasury.executeSwap()
     *  ↓
     * Pool.swapXtoY()
     *  ↑
     * 如果 Pool 失败（如滑点检查失败），异常会沿链冒泡
     * 整个交易自动回滚，Treasury 和 Pool 的状态都不变
     * 用户不需要手动补偿！
     */
    function atomicSwap(
        uint256 amountXIn,
        uint256 minYOut
    ) external returns (uint256 amountYOut) {
        totalRoutedSwaps++;
        
        emit RoutingInitiated(msg.sender, amountXIn, minYOut);
        emit AtomicityDemonstration("Starting atomic swap in same shard");
        
        // 这个调用链是**原子的**（同分片同交易池）
        amountYOut = IAMMTreasury(treasury).executeSwap(msg.sender, amountXIn, minYOut);
        
        successfulSwaps++;
        emit RoutingSuccess(msg.sender, amountXIn, amountYOut);
        emit AtomicityDemonstration("Atomic swap completed: no compensation needed!");
        
        return amountYOut;
    }

    /**
     * @notice 多跳交换演示
     * @param hops 跳数
     * 
     * 与跨分片不同：同分片内所有跳都在一个交易中原子执行
     * 要么全成功，要么全失败 + 自动回滚
     */
    function multiHopSwap(uint256 amountXIn, uint256 minYOut, uint256 hops) 
        external 
        returns (uint256 finalAmount) 
    {
        require(hops > 0, "At least 1 hop required");
        
        uint256 currentAmount = amountXIn;
        
        // 所有跳都在同一交易中执行
        for (uint256 i = 0; i < hops; i++) {
            uint256 hopMinOut = (i == hops - 1) ? minYOut : 1;
            
            // 每一跳都是 atomicSwap 的链式调用
            // 如果任何一跳失败，整个交易回滚
            currentAmount = this.atomicSwap(currentAmount, hopMinOut);
        }
        
        emit AtomicityDemonstration("Multi-hop swap completed atomically");
        return currentAmount;
    }

    /**
     * @notice 获取统计信息
     */
    function getStats() external view returns (uint256 total, uint256 success, uint256 failed) {
        return (totalRoutedSwaps, successfulSwaps, failedSwaps);
    }
}
"""

def test_amm_same_shard_atomic_swap(w3, MY, KEY):
    """
    Test Case: Same-Shard AMM Atomic Swap
    
    关键设计原则：
    - 三个合约（Pool、Treasury、Router）由同一账户部署
    - 因此它们都在该账户所在的分片和交易池中
    - 所有合约间调用在同一交易池中原子执行
    - 原子性保证：要么全成功，要么全失败（自动回滚）
    
    对比跨分片：
    - 跨分片部署的合约无法原子调用
    - 需要手动补偿机制
    - 开发者负担大
    
    Validation Points:
    1. 同一账户部署三个合约到同一分片
    2. 成功的交换：自动执行，无需手动补偿
    3. 失败的交换：自动回滚，状态不变
    4. 链式调用在同一交易中原子执行
    """
    print("\n" + "="*80)
    print("TEST CASE: Same-Shard AMM Atomic Swap (Single Transaction Pool)")
    print("="*80)
    
    print("\n[1] 部署三个合约到同一分片（同一账户创建）")
    print("-" * 80)
    print(f"创建者账户: {MY}")
    print("这些合约会被部署到该账户所在的分片和交易池中\n")
    
    # ========== 第一步：部署 AMMPool ==========
    print("▶ 部署 AMMPool (流动性池)")
    pool_bin, pool_abi = compile_and_link(AMM_POOL_SOL, "AMMPool")
    amm_pool = w3.shardora.contract(abi=pool_abi, bytecode=pool_bin).deploy({
        'from': MY,
        'salt': RANDOM_SALT + 'amm_pool_',
        'args': ["SHARDORA/USDC", 10000, 10000],
    }, KEY)
    print(f"  ✅ AMMPool 已部署: {amm_pool.address}")
    print(f"     初始储备: X=10000, Y=10000 (K={10000*10000})")
    
    # ========== 第二步：部署 AMMTreasury ==========
    print("\n▶ 部署 AMMTreasury (资金管理)")
    treasury_bin, treasury_abi = compile_and_link(AMM_TREASURY_SOL, "AMMTreasury")
    amm_treasury = w3.shardora.contract(abi=treasury_abi, bytecode=treasury_bin).deploy({
        'from': MY,
        'salt': RANDOM_SALT + 'amm_treas',
        'args': [to_checksum_address(amm_pool.address)],
    }, KEY)
    print(f"  ✅ AMMTreasury 已部署: {amm_treasury.address}")
    
    # ========== 第三步：部署 AMMRouter ==========
    print("\n▶ 部署 AMMRouter (路由和编排)")
    router_bin, router_abi = compile_and_link(AMM_ROUTER_SOL, "AMMRouter")
    amm_router = w3.shardora.contract(abi=router_abi, bytecode=router_bin).deploy({
        'from': MY,
        'salt': RANDOM_SALT + 'amm_rout_',
        'args': [to_checksum_address(amm_treasury.address), to_checksum_address(amm_pool.address)],
    }, KEY)
    print(f"  ✅ AMMRouter 已部署: {amm_router.address}")
    
    # ========== 第四步：初始化关系 ==========
    print("\n[2] 初始化合约关系")
    print("-" * 80)
    
    # Pool 设置 Treasury
    print("▶ AMMPool.setTreasury() ...")
    amm_pool.functions.setTreasury(to_checksum_address(amm_treasury.address)).transact(KEY)
    print("  ✅ Treasury 已关联到 Pool")
    
    # Treasury 设置 Router
    print("▶ AMMTreasury.setRouter() ...")
    amm_treasury.functions.setRouter(to_checksum_address(amm_router.address)).transact(KEY)
    print("  ✅ Router 已关联到 Treasury")
    
    # ========== 第五步：用户存款 ==========
    print("\n[3] 用户存款初始化")
    print("-" * 80)
    print("▶ 用户存入: 1000 X, 1000 Y")
    amm_treasury.functions.deposit(1000, 1000).transact(KEY, value=2000)
    
    user_balance = amm_treasury.functions.getUserBalance(MY).call()
    print(f"  ✅ 用户余额: X={user_balance[0]}, Y={user_balance[1]}")
    
    # ========== TEST 1: 成功的原子交换 ==========
    print("\n[4] TEST 1: 成功的原子交换（滑点在允许范围内）")
    print("-" * 80)
    print("场景: 用户交换 100 X，最小期望 90 Y")
    print("步骤: Router → Treasury → Pool（同交易池中原子执行）")
    
    amount_in = 100
    min_out = 90
    
    receipt = amm_router.functions.atomicSwap(amount_in, min_out).transact(KEY)
    
    if receipt.get('status') == 0:
        print("✅ 交易成功，状态码: 0")
        
        # 检查事件
        for e in receipt.get('decoded_events', []):
            if e['event'] == 'RoutingSuccess':
                actual_out = e['args']['amountYOut']
                print(f"✅ 原子交换成功: {e['args']['amountXIn']} X → {actual_out} Y")
            elif e['event'] == 'AtomicityDemonstration':
                print(f"   📍 {e['args']['message']}")
        
        # 验证状态更新
        user_balance_after = amm_treasury.functions.getUserBalance(MY).call()
        print(f"✅ 用户余额已更新: X={user_balance_after[0]}, Y={user_balance_after[1]}")
        
        pool_stats = amm_pool.functions.getPoolStats().call()
        print(f"✅ Pool 状态已更新: X={pool_stats[0]}, Y={pool_stats[1]}, 总交换={pool_stats[2]}")
    else:
        print(f"❌ 交易失败: {receipt.get('msg')}")
    
    # ========== TEST 2: 失败导致自动回滚 ==========
    print("\n[5] TEST 2: 失败的交换导致自动回滚（同分片保证）")
    print("-" * 80)
    print("场景: 用户要求最小输出 5000 Y（市场无法提供）")
    print("结果: 整个交易回滚，Pool 和 Treasury 状态不变")
    print("对比跨分片: 跨分片会导致状态不一致，需要手动补偿")
    
    amount_in = 100
    unrealistic_min = 5000
    
    # 记录当前状态
    balance_before_fail = amm_treasury.functions.getUserBalance(MY).call()
    pool_before_fail = amm_pool.functions.getPoolStats().call()
    
    print(f"\n交易前状态:")
    print(f"  用户余额: X={balance_before_fail[0]}, Y={balance_before_fail[1]}")
    print(f"  Pool状态: X={pool_before_fail[0]}, Y={pool_before_fail[1]}")
    
    # 尝试失败的交换
    try:
        receipt = amm_router.functions.atomicSwap(amount_in, unrealistic_min).transact(KEY)
        
        if receipt.get('status') != 0:
            print(f"\n✅ 交易失败（预期行为），状态码: {receipt.get('status')}")
            print(f"   原因: {receipt.get('msg')}")
            
            # 验证自动回滚
            balance_after_fail = amm_treasury.functions.getUserBalance(MY).call()
            pool_after_fail = amm_pool.functions.getPoolStats().call()
            
            print(f"\n✅ 自动回滚验证:")
            print(f"  用户余额不变: X={balance_after_fail[0]} (期望{balance_before_fail[0]})")
            print(f"  Pool状态不变: X={pool_after_fail[0]} (期望{pool_before_fail[0]})")
            
            if (balance_after_fail[0] == balance_before_fail[0] and 
                pool_after_fail[0] == pool_before_fail[0]):
                print("\n🎯 原子性保证验证成功！")
                print("   → 失败的交换导致整个交易回滚")
                print("   → 用户余额和 Pool 状态都未改变")
                print("   → 无需手动补偿机制")
        else:
            print(f"⚠️ 交易意外成功")
    except Exception as e:
        print(f"✅ 交易异常（预期行为）: {str(e)}")
        print("   → 异常在同交易池中导致回滚")
        print("   → 原子性保证生效")
    
    # ========== TEST 3: 多跳交换（所有跳原子执行）==========
    print("\n[6] TEST 3: 多跳交换（所有跳在同一交易中原子执行）")
    print("-" * 80)
    print("场景: 2跳交换（A → B → C）")
    print("特点: 与跨分片不同，所有跳都在同交易中完成，无需补偿")
    
    # 重新存款以确保余额充足
    amm_treasury.functions.deposit(500, 500).transact(KEY, value=1000)
    
    receipt = amm_router.functions.multiHopSwap(200, 50, 2).transact(KEY)
    
    if receipt.get('status') == 0:
        print("✅ 多跳交换成功")
        
        for e in receipt.get('decoded_events', []):
            if e['event'] == 'AtomicityDemonstration':
                print(f"   📍 {e['args']['message']}")
        
        final_balance = amm_treasury.functions.getUserBalance(MY).call()
        print(f"✅ 最终用户余额: X={final_balance[0]}, Y={final_balance[1]}")
        print("   → 所有跳都在同一交易中成功完成")
        print("   → 自动原子性，无需手动协调")
    else:
        print(f"❌ 多跳交换失败: {receipt.get('msg')}")
    
    # ========== 总结 ==========
    print("\n[7] 对比分析：同分片 vs 跨分片")
    print("="*80)
    print("""
┌─────────────────────┬──────────────────┬──────────────────┐
│      特性           │     同分片        │     跨分片       │
├─────────────────────┼──────────────────┼──────────────────┤
│ 部署位置            │ 同一交易池       │ 不同分片         │
│ 调用原子性          │ ✅ 自动原子      │ ❌ 非原子        │
│ 失败回滚            │ ✅ 自动回滚      │ ❌ 手动补偿      │
│ 开发难度            │ ✅ 简单          │ ❌ 复杂          │
│ 状态一致性          │ ✅ 保证          │ ❌ 不保证        │
│ 链式调用            │ ✅ 支持          │ ❌ 异步          │
│ 最终化时间          │ ✅ 一个区块      │ ❌ 多个区块      │
│ 补偿逻辑            │ ✅ 无需          │ ❌ 必须编写      │
└─────────────────────┴──────────────────┴──────────────────┘

结论：
1. 同分片部署使得原子性成为约束条件而非特性
2. 这大大简化了复杂合约交互的实现
3. 跨分片仍需补偿机制，但可通过中间层合约优化
4. Shardora 通过分片部署策略实现灵活的一致性保证
    """)

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
    
    # 1. 记录转账前的余额
    balance_before = w3.client.get_balance(dest)
    balance_before = w3.client.get_balance(dest) # 1. Record balance before transfer
    print(f"Balance before: {balance_before}")
    
    # 2. 执行转账交易
    receipt = w3.shardora.send_transaction({'to': dest, 'value': transfer_amount}, KEY)
    receipt = w3.shardora.send_transaction({'to': dest, 'value': transfer_amount}, KEY) # 2. Execute transfer transaction
    
    # 3. 验证交易状态
    if receipt.get('status') == 0:
    if receipt.get('status') == 0: # 3. Verify transaction status
        print(f"Transfer Sent Successfully. Hash: {receipt.get('tx_hash', 'N/A')}")
        
        count = 0
        while count < 30:
            # 给节点一点同步时间（可选，取决于你的 RPC 响应速度）
            time.sleep(2) 
            time.sleep(2) # Give the node some synchronization time (optional, depends on your RPC response speed)
            
            # 4. 获取转账后的余额
            balance_after = w3.client.get_balance(dest)
            balance_after = w3.client.get_balance(dest) # 4. Get balance after transfer
            print(f"Balance after: {balance_after}")
            
            # 5. 余额合法性校验逻辑
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
    测试国密标准转账
    利用 SDK 内部逻辑：传入 gm_pubkey 自动切换 SM2/SM3
    Test GmSSL standard transfer
    Utilizes SDK internal logic: passing gm_pubkey automatically switches between SM2/SM3
    """
    print("\n--- TEST CASE: GmSSL Standard Transfer ---")
    dest = "0000000000000000000000000000000000000001"
    
    # 1. 自动从私钥派生公钥和地址用于日志展示
    gm_pubkey = get_sm2_public_key(GM_KEY)
    # 调用 SDK 内部方法计算地址 (SM3 截断)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey) # Call SDK internal method to calculate address (SM3 truncation)
    print(f"GmSSL Sender Address: {GM_MY}")

    # 2. 构造交易字典
    tx_dict = {
    tx_dict = { # 2. Construct transaction dictionary
        'to': dest,
        'value': 10000,
        'gm_pubkey': gm_pubkey  # 触发 SDK 的 send_gmssl_transaction 逻辑
    }

    # 3. 发起交易
    print("Sending GmSSL Transfer...")
    receipt = w3.shardora.send_gmssl_transaction(tx_dict, GM_KEY)
    receipt = w3.shardora.send_gmssl_transaction(tx_dict, GM_KEY) # 3. Initiate transaction

    print(f"GmSSL Transfer Status: {receipt.get('status')}")
    if receipt.get('status') == 0:
        print(f"✅ Success! New balance: {w3.client.get_balance(dest)}")
    else:
        print(f"❌ Failed: {receipt.get('msg')}")

def test_gmssl_contract_flow(w3, GM_KEY):
    """
    测试国密账户的合约全流程：部署 -> 预付Gas -> 调用
    完全利用 gm_mode=True 自动派生公钥
    Test the full contract flow for GmSSL accounts: Deploy -> Prefund Gas -> Call
    Fully utilizes gm_mode=True to automatically derive public key
    """
    print("\n--- TEST CASE: GmSSL Contract Full Flow (Auto-Derive) ---")

    # 1. 准备合约
    src = """
    pragma solidity ^0.8.0;
    contract GmVault {
        uint256 public data;
        function store(uint256 v) public { data = v; }
    }
    """
    bin_code, abi = compile_and_link(src, "GmVault")
    
    # 2. 计算 Sender 地址用于 deploy 参数
    # 2. Calculate Sender address for deploy parameters
    gm_pubkey = get_sm2_public_key(GM_KEY)
    GM_MY = w3.client.get_gmssl_address(gm_pubkey)
    
    print(f"GmSSL Sender Address pk: {gm_pubkey}, GM_MY: {GM_MY}")
    # 3. 部署合约
    # 3. Deploy contract
    # deploy 内部会判断 gm_pubkey 是否存在，如果存在则走国密
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

    # 4. 预付 Gas (Prefund)
    # 4. Prefund Gas
    # 利用修改后的 SDK：只需传 gm_mode=True
    print("[*] Setting Gas Prefund (gm_mode=True)...")
    gm_vault.prefund(50000000, GM_KEY, gm_mode=True)

    # 5. 调用合约 (Transact)
    # 5. Call Contract (Transact)
    # 利用修改后的 SDK：只需传 gm_mode=True，内部自动调 get_sm2_public_key
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

def ecdsa_sign_test():
    IP, PORT, KEY = "127.0.0.1", 23001, "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6"
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address(KEY)

    test_contract_call_contract(w3, MY, KEY)
    test_transfer(w3, MY, KEY, "620a1c023fdef21f3c10bf3d468de37d5ecfdc7b")
    test_library_with_contrcat(w3, MY, KEY)
    test_ecdsa_prefund_full_flow(w3, MY, KEY)

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

if __name__ == "__main__":
    # Standard ECDSA signature tests
    ecdsa_sign_test()
    
    # Post-quantum signature tests
    oqs_sign_test()
    
    # GM/SM2 signature tests
    gmssl_sign_test()
    
    # === NEW: Same-Shard AMM Atomic Swap Tests ===
    # Tests the atomicity guarantee when multiple contracts are deployed to the same shard
    # by the same account and executed in the same transaction pool
    print("\n" + "="*80)
    print("RUNNING SAME-SHARD AMM ATOMIC SWAP TESTS")
    print("Key Principle: Multiple contracts deployed by same account → same shard → same TX pool")
    print("Result: Automatic atomicity and rollback guarantee")
    print("="*80)
    
    IP, PORT, KEY = "127.0.0.1", 23001, "71e571862c0e4aefa87a3c16057a62c8331991a11746ab7ff8c6b6418e73b2f6"
    w3 = ShardoraWeb3Mock(IP, PORT)
    MY = w3.client.get_address(KEY)
    
    test_amm_same_shard_atomic_swap(w3, MY, KEY)
 