// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IERC20 {
    function transferFrom(address sender, address recipient, uint256 amount) external returns (bool);
    function transfer(address recipient, uint256 amount) external returns (bool);
}

contract DIRMStableSwap {
    IERC20 public immutable tokenX; // USDC
    IERC20 public immutable tokenY; // sUSDC

    // Curve Parameters
    uint256 public constant A = 100;
    
    // DIRM Parameters (1e18 precision)
    int256 public constant TAU = 2e16;       // 0.02
    int256 public constant K = 30e18;        // 30.0
    int256 public constant R_MAX = 5e16;     // 0.05
    int256 public constant TARGET_P = 1e18;  // 1.00

    // State Variables
    uint256 public reserveX;
    uint256 public reserveY;
    uint256 public treasuryX;
    uint256 public treasuryY;

    constructor(address _tokenX, address _tokenY) {
        tokenX = IERC20(_tokenX);
        tokenY = IERC20(_tokenY);
    }

    // --- Fixed Point Math Helpers (1e18) ---

    function mulWad(int256 a, int256 b) internal pure returns (int256) {
        return (a * b) / 1e18;
    }

    function divWad(int256 a, int256 b) internal pure returns (int256) {
        return (a * 1e18) / b;
    }

    // Padé Approximant for tanh(x)
    function tanh(int256 x) internal pure returns (int256) {
        if (x == 0) return 0;
        bool isNegative = x < 0;
        int256 absX = isNegative ? -x : x;
        
        // Saturation at extremes to enforce mathematical bounds
        if (absX >= 3e18) {
            return isNegative ? -int256(1e18) : int256(1e18);
        }

        int256 x2 = mulWad(absX, absX);
        int256 x3 = mulWad(x2, absX);
        
        int256 num = x3 + (15 * absX);
        int256 den = (6 * x2) + 15e18;
        
        int256 result = divWad(num, den);
        if (result > 1e18) result = 1e18; // Hard cap
        
        return isNegative ? -result : result;
    }

    // --- Core Curve Math ---

    function getD(uint256 x, uint256 y) public pure returns (uint256) {
        uint256 S = x + y;
        if (S == 0) return 0;
        uint256 D = S;
        uint256 Ann = A * 4; // N^N = 4 for 2 coins
        
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

    // --- DIRM Logic ---

    function getP(uint256 x, uint256 y, uint256 D) public pure returns (uint256) {
        // Reduced algebraic formula to prevent x^2 * y^2 overflow
        uint256 term1 = (16 * A * x) / D * y;
        uint256 term2_num = (D * D) / y;
        uint256 term2_den = (D * D) / x;
        
        uint256 num = term1 + term2_num;
        uint256 den = term1 + term2_den;
        
        return (num * 1e18) / den;
    }

    function getR(uint256 currentP) public pure returns (int256) {
        int256 d = int256(currentP) - TARGET_P;
        
        // State 1: Equilibrium Dead Zone
        if (d >= -TAU && d <= TAU) {
            return 0;
        }
        
        // States 2 & 3: Shifted deviation
        int256 d_prime = d > 0 ? d - TAU : d + TAU;
        int256 z = mulWad(K, d_prime);
        int256 t = tanh(z);
        
        return mulWad(R_MAX, t);
    }

    // --- Execution Interface ---

    // Swap X (USDC) for Y (sUSDC)
    function swapUSDCForSUSDC(uint256 dx) external returns (uint256 dy_actual) {
        require(dx > 0, "Zero amount");
        tokenX.transferFrom(msg.sender, address(this), dx);

        uint256 D = getD(reserveX, reserveY);
        uint256 y_new = getY(reserveX + dx, D);
        uint256 dy_curve = reserveY - y_new;

        uint256 P_curve = (dx * 1e18) / dy_curve;
        uint256 P_marginal = getP(reserveX, reserveY, D);
        int256 R = getR(P_marginal);

        // Calculate Effective Price: P_eff = P_curve + R
        int256 P_eff = int256(P_curve) + R;
        require(P_eff > 0, "Invalid effective price");
        
        dy_actual = (dx * 1e18) / uint256(P_eff);

        if (dy_actual < dy_curve) {
            // Penalty: Pool keeps the difference
            treasuryY += (dy_curve - dy_actual);
        } else if (dy_actual > dy_curve) {
            // Subsidy: Draw from Treasury (Hard bounded by available treasury to protect invariant)
            uint256 subsidy = dy_actual - dy_curve;
            if (subsidy > treasuryY) {
                subsidy = treasuryY;
                dy_actual = dy_curve + subsidy;
            }
            treasuryY -= subsidy;
        }

        reserveX += dx;
        reserveY -= dy_curve; // Invariant tracks theoretical curve

        tokenY.transfer(msg.sender, dy_actual);
    }
}