# C2CSellOrder Contract Test Suite

## Overview

This comprehensive test suite provides complete testing coverage for the C2CSellOrder smart contract, including deployment, all contract functions, edge cases, and security scenarios.

## Features

- **Complete Contract Testing**: Tests all 15+ contract functions
- **Deployment Testing**: Automated contract compilation and deployment
- **Edge Case Testing**: Tests error conditions and boundary cases
- **Security Testing**: Validates access controls and security measures
- **Workflow Testing**: Tests complete C2C transaction workflows
- **Comprehensive Reporting**: Detailed test results with timing and success rates

## Contract Functions Tested

### Core Functions
- `constructor()` - Contract initialization with managers and limits
- `TestContract()` - Test function for basic contract interaction
- `callAbe()` - RIPEMD160 hash function testing
- `SetManager()` - Manager management functionality
- `NewSellOrder()` - Create new sell orders with pledge
- `Confirm()` - Confirm transactions and transfer funds
- `ManagerRelease()` - Manager-initiated fund release
- `ManagerReleaseForce()` - Forced release by managers
- `SellerRelease()` - Seller-initiated release
- `Report()` - Report problematic sellers

### Utility Functions
- `bytesConcat()` - Concatenate byte arrays
- `ToHex()` - Convert bytes to hex string
- `toBytes()` - Convert address to bytes
- `u256ToBytes()` - Convert uint256 to bytes
- `GetOrderJson()` - Generate JSON for single order
- `GetOrdersJson()` - Generate JSON for all orders

## Test Categories

### 1. Basic Function Tests
- Contract deployment and initialization
- Individual function calls with valid parameters
- Event emission verification
- State variable validation

### 2. Edge Case Tests
- Insufficient pledge amounts
- Manager restrictions on sell orders
- Minimum exchange amount validation
- Access control violations

### 3. Workflow Tests
- Complete C2C transaction flow
- Order creation → Confirmation → Manager release → Seller release
- Multi-step transaction validation

### 4. Security Tests
- Owner-only functions (SetManager)
- Manager-only functions (ManagerRelease)
- Double reporting prevention
- Access control enforcement

## Usage

### Prerequisites

1. **Shardora Node Running**: Ensure your Shardora blockchain node is running
2. **Python Dependencies**: Install required packages:
   ```bash
   cd clipy
   pip install -r requirements.txt  # If available
   # Or install manually:
   pip install requests eth-abi solcx ecdsa pycryptodome
   ```

### Running the Tests

1. **Basic Test Run**:
   ```bash
   cd clipy
   python test_c2c_contract.py
   ```

2. **Custom Shardora Node Connection**:
   ```python
   # Edit the main() function in test_c2c_contract.py
   HOST = "your-shardora-node-ip"
   PORT = 9001  # Your Shardora node port
   ```

### Test Configuration

The test suite automatically generates test accounts for different roles:
- **Owner**: Contract deployer and manager setter
- **Manager1/Manager2**: Authorized managers for releases
- **Seller1/Seller2**: Users creating sell orders
- **Buyer1/Buyer2**: Users purchasing from sellers

### Expected Output

```
🚀 C2CSellOrder Contract Test Suite
================================================================================
📡 Connected to Shardora node: 127.0.0.1:9001
  📝 Generated owner: a1b2c3d4...
  📝 Generated manager1: e5f6g7h8...
  ...

==================== Contract Deployment ====================
🧪 Testing Contract Deployment...
  📦 Compiling C2CSellOrder contract...
  ✅ Contract compiled successfully
  🚀 Deploying contract...
  ✅ Contract deployed at: 1a2b3c4d5e6f...
✅ Contract Deployment PASSED (2.34s)

==================== Contract Initialization ====================
🧪 Testing Contract Initialization...
  📋 Owner: a1b2c3d4... (expected: a1b2c3d4...)
  📋 Min Pledge: 1000000
  📋 Min Exchange: 100000
  ✅ Initialization test: PASSED
✅ Contract Initialization PASSED (0.45s)

...

================================================================================
📋 FINAL TEST RESULTS SUMMARY
================================================================================
Contract Deployment               ✅ PASS     (2.34s)
Contract Initialization           ✅ PASS     (0.45s)
TestContract Function             ✅ PASS     (1.23s)
...
--------------------------------------------------------------------------------
Overall Result: 15/15 tests passed
Total Duration: 45.67 seconds
Success Rate: 100.0%
🎉 ALL TESTS PASSED! C2CSellOrder contract is fully functional.
```

## Test Results Interpretation

### Success Indicators
- ✅ **PASS**: Test completed successfully
- 📋 **Info**: Informational output
- 📊 **Stats**: Statistical information

### Failure Indicators
- ❌ **FAIL**: Test failed with specific reason
- 💥 **ERROR**: Unexpected error occurred
- ⚠️ **WARNING**: Partial success or issues detected

### Success Rates
- **100%**: All tests passed - contract fully functional
- **80-99%**: Most tests passed - minor issues may exist
- **<80%**: Significant issues detected - review required

## Troubleshooting

### Common Issues

1. **Connection Failed**:
   ```
   Error: Connection refused to 127.0.0.1:9001
   ```
   - Ensure Shardora node is running
   - Check host/port configuration
   - Verify network connectivity

2. **Compilation Errors**:
   ```
   Error: Solidity compiler not found
   ```
   - Install solc: `npm install -g solc`
   - Or use solcx: `pip install py-solc-x`

3. **Transaction Failures**:
   ```
   Status: 5010 (Account balance error)
   ```
   - Ensure test accounts have sufficient balance
   - Check gas limits and fees

### Debug Mode

Enable verbose logging by modifying the test file:
```python
# Add at the top of test functions
print(f"DEBUG: Function parameters: {locals()}")
```

## Contract Specifications

### Minimum Values
- **Minimum Pledge**: 1,000,000 wei
- **Minimum Exchange**: 100,000 wei

### Key Features
- **Pledge-based Trading**: Sellers must pledge funds
- **Manager Oversight**: Managers can release funds
- **Reporting System**: Users can report problematic sellers
- **JSON Export**: Orders can be exported as JSON

### Security Features
- Owner-only manager management
- Manager-only fund releases
- Pledge amount validation
- Double-reporting prevention

## Contributing

To add new tests:

1. Create a new test method in the `C2CContractTest` class
2. Follow the naming convention: `test_[feature_name](self) -> bool`
3. Add the test to the `tests` list in `run_all_tests()`
4. Include proper error handling and logging

Example:
```python
def test_new_feature(self) -> bool:
    """Test description"""
    print("\n🧪 Testing New Feature...")
    
    try:
        # Test implementation
        result = self.contract.functions.newFunction().call()
        success = result is not None
        print(f"  ✅ New feature: {'PASSED' if success else 'FAILED'}")
        return success
    except Exception as e:
        print(f"  ❌ New feature failed: {str(e)}")
        return False
```

## License

This test suite is provided under the same license as the main project.