// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * @title sUSDC - Shardora Chain USDC
 * @notice Cross-chain mapped USDC token with mint/burn support
 * @dev Ownable functionality inlined, no external dependencies
 *      Includes DIRM curve trading functionality
 *
 *      All ERC20 `amount` values use **6 decimals** (USDC-like). Internal DIRM `SCALE_*` is 1e6 so
 *      fixed-point math matches token decimals. Virtual pool legs (reserveX / reserveY) use the same raw scale.
 */
contract sUSDC {
    /// @dev DIRM fixed-point uses sUSDC decimal scale (1e6).
    uint256 private constant SCALE_U = 1_000_000;
    int256 private constant SCALE_S = int256(uint256(1_000_000));

    // ==================== Ownable Functionality (Inlined) ====================
    
    address private _owner;

    event OwnershipTransferred(address indexed previousOwner, address indexed newOwner);

    modifier onlyOwner() {
        require(msg.sender == _owner, "sUSDC: Not owner");
        _;
    }

    function owner() public view returns (address) {
        return _owner;
    }

    function renounceOwnership() public onlyOwner {
        emit OwnershipTransferred(_owner, address(0));
        _owner = address(0);
    }

    function transferOwnership(address newOwner) public onlyOwner {
        require(newOwner != address(0), "sUSDC: New owner is zero address");
        emit OwnershipTransferred(_owner, newOwner);
        _owner = newOwner;
    }

    // ==================== ERC20 Basic Functionality ====================
    
    string public constant name = "Shardora USDC";
    string public constant symbol = "sUSDC";
    uint8 public constant decimals = 6;
    uint256 public totalSupply;
    
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;
    
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);
    event Mint(address indexed to, uint256 value);
    event Burn(address indexed from, uint256 value);
    event MinterAdded(address indexed minter);
    event MinterRemoved(address indexed minter);

    // ==================== Mint Permission Management ====================
    
    mapping(address => bool) public minters;
    
    modifier onlyMinter() {
        require(minters[msg.sender], "sUSDC: Not minter");
        _;
    }
    
    function addMinter(address _minter) external onlyOwner {
        minters[_minter] = true;
        emit MinterAdded(_minter);
    }
    
    function removeMinter(address _minter) external onlyOwner {
        minters[_minter] = false;
        emit MinterRemoved(_minter);
    }

    // ==================== Constructor ====================

    constructor() {
        _owner = msg.sender;
        emit OwnershipTransferred(address(0), msg.sender);
    }

    // ==================== ERC20 Core Functions ====================

    function transfer(address to, uint256 amount) external returns (bool) {
        require(to != address(0), "sUSDC: Transfer to zero address");
        require(balanceOf[msg.sender] >= amount, "sUSDC: Insufficient balance");
        
        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;
        
        emit Transfer(msg.sender, to, amount);
        return true;
    }
    
    function approve(address spender, uint256 amount) external returns (bool) {
        allowance[msg.sender][spender] = amount;
        emit Approval(msg.sender, spender, amount);
        return true;
    }
    
    function transferFrom(address from, address to, uint256 amount) external returns (bool) {
        require(from != address(0), "sUSDC: Transfer from zero address");
        require(to != address(0), "sUSDC: Transfer to zero address");
        require(balanceOf[from] >= amount, "sUSDC: Insufficient balance");
        require(allowance[from][msg.sender] >= amount, "sUSDC: Insufficient allowance");
        
        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        allowance[from][msg.sender] -= amount;
        
        emit Transfer(from, to, amount);
        return true;
    }

    // ==================== Mint/Burn (Minter Only) ====================

    function mint(address to, uint256 amount) external onlyMinter {
        require(to != address(0), "sUSDC: Mint to zero address");
        
        totalSupply += amount;
        balanceOf[to] += amount;
        
        emit Mint(to, amount);
        emit Transfer(address(0), to, amount);
    }
    
    function burn(address from, uint256 amount) external onlyMinter {
        require(from != address(0), "sUSDC: Burn from zero address");
        require(balanceOf[from] >= amount, "sUSDC: Insufficient balance");
        
        totalSupply -= amount;
        balanceOf[from] -= amount;
        
        emit Burn(from, amount);
        emit Transfer(from, address(0), amount);
    }

    // ==================== DIRM Curve Trading Parameters ====================
    
    // Curve Parameters
    uint256 public constant A = 100;
    
    // DIRM Parameters (SCALE = 1e6 fixed-point)
    int256 public constant TAU = 20_000; // 0.02 * SCALE
    int256 public constant K = 30 * SCALE_S; // 30.0 * SCALE
    int256 public constant R_MAX = 50_000; // 0.05 * SCALE
    int256 public constant TARGET_P = SCALE_S; // 1.00 * SCALE

    // State Variables (internal ledger; amounts in 6-decimal raw, same as ERC20)
    uint256 public reserveX;  // virtual "X" leg (same decimal scale as sUSDC)
    uint256 public reserveY;  // virtual sUSDC reserve
    uint256 public treasuryX; // Treasury X
    uint256 public treasuryY; // Treasury Y
    
    // Internal ledger (for DIRM trading)
    mapping(address => uint256) public balanceOfX; // Virtual USDC balance
    mapping(address => uint256) public balanceOfY; // Virtual sUSDC balance

    // ==================== Faucet (For Testing) ====================

    function faucet(uint256 amountX, uint256 amountY) external {
        balanceOfX[msg.sender] += amountX;
        balanceOfY[msg.sender] += amountY;
    }

    // ==================== Initialize Pool ====================

    function initializePool(uint256 initX, uint256 initY) external {
        require(reserveX == 0 && reserveY == 0, "Pool already initialized");
        require(balanceOfX[msg.sender] >= initX && balanceOfY[msg.sender] >= initY, "Insufficient balance to init");
        
        balanceOfX[msg.sender] -= initX;
        balanceOfY[msg.sender] -= initY;
        
        reserveX = initX;
        reserveY = initY;
    }

    // ==================== Fixed-Point Math Helper Functions ====================

    function mulWad(int256 a, int256 b) internal pure returns (int256) {
        return (a * b) / SCALE_S;
    }

    function divWad(int256 a, int256 b) internal pure returns (int256) {
        return (a * SCALE_S) / b;
    }

    // Padé approximation tanh(x)
    function tanh(int256 x) internal pure returns (int256) {
        if (x == 0) return 0;
        bool isNegative = x < 0;
        int256 absX = isNegative ? -x : x;
        
        if (absX >= 3 * SCALE_S) {
            return isNegative ? -SCALE_S : SCALE_S;
        }

        int256 x2 = mulWad(absX, absX);
        int256 x3 = mulWad(x2, absX);
        
        int256 num = x3 + (15 * absX);
        int256 den = (6 * x2) + (15 * SCALE_S);
        
        int256 result = divWad(num, den);
        if (result > SCALE_S) result = SCALE_S;
        
        return isNegative ? -result : result;
    }

    // ==================== Core Curve Math ====================

    function getD(uint256 x, uint256 y) public pure returns (uint256) {
        uint256 S = x + y;
        if (S == 0) return 0;
        uint256 D = S;
        uint256 Ann = A * 4;
        
        for (uint256 i = 0; i < 255; i++) {
            uint256 D_P = D;
            D_P = (D_P * D) / (x * 2);
            D_P = (D_P * D) / (y * 2);
            uint256 Dprev = D;
            
            uint256 num = (Ann * S + D_P * 2) * D;
            uint256 den = (Ann - 1) * D + 3 * D_P;
            D = num / den;
            
            if (D > Dprev && D - Dprev <= 1) break;
            if (Dprev >= D && Dprev - D <= 1) break;
        }
        return D;
    }

    function getY(uint256 x_new, uint256 D) internal pure returns (uint256) {
        uint256 Ann = A * 4;
        uint256 c = (D * D) / (x_new * 2);
        c = (c * D) / (Ann * 2);
        uint256 b = x_new + D / Ann;
        
        uint256 y = D;
        for (uint256 i = 0; i < 255; i++) {
            uint256 y_prev = y;
            y = (y * y + c) / (2 * y + b - D);
            
            if (y > y_prev && y - y_prev <= 1) break;
            if (y_prev >= y && y_prev - y <= 1) break;
        }
        return y;
    }

    // ==================== DIRM Logic ====================

    function getP(uint256 x, uint256 y, uint256 D) public pure returns (uint256) {
        uint256 term1 = (16 * A * x) / D * y;
        uint256 term2_num = (D * D) / y;
        uint256 term2_den = (D * D) / x;
        
        uint256 num = term1 + term2_num;
        uint256 den = term1 + term2_den;
        
        return (num * SCALE_U) / den;
    }

    function getR(uint256 currentP) public pure returns (int256) {
        int256 d = int256(currentP) - TARGET_P;
        
        if (d >= -TAU && d <= TAU) {
            return 0;
        }
        
        int256 d_prime = d > 0 ? d - TAU : d + TAU;
        int256 z = mulWad(K, d_prime);
        int256 t = tanh(z);
        
        return mulWad(R_MAX, t);
    }

    // ==================== Trading Interface ====================

    function swapUSDCForSUSDC(uint256 dx) external returns (uint256 dy_actual) {
        require(dx > 0, "Zero amount");
        
        require(balanceOfX[msg.sender] >= dx, "Insufficient virtual USDC balance");
        balanceOfX[msg.sender] -= dx;

        uint256 D = getD(reserveX, reserveY);
        uint256 y_new = getY(reserveX + dx, D);
        uint256 dy_curve = reserveY - y_new;

        uint256 P_curve = (dx * SCALE_U) / dy_curve;
        uint256 P_marginal = getP(reserveX, reserveY, D);
        int256 R = getR(P_marginal);

        int256 P_eff = int256(P_curve) + R;
        require(P_eff > 0, "Invalid effective price");
        
        dy_actual = (dx * SCALE_U) / uint256(P_eff);

        if (dy_actual < dy_curve) {
            treasuryY += (dy_curve - dy_actual);
        } else if (dy_actual > dy_curve) {
            uint256 subsidy = dy_actual - dy_curve;
            if (subsidy > treasuryY) {
                subsidy = treasuryY;
                dy_actual = dy_curve + subsidy;
            }
            treasuryY -= subsidy;
        }

        reserveX += dx;
        reserveY -= dy_curve;

        balanceOfY[msg.sender] += dy_actual;
    }
}