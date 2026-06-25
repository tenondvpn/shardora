# Transaction Step Type Reference

## Overview

This document explains the correct usage of transaction step types (`StepType`) in the Shardora blockchain system.

## Step Type Definitions

From `shardora_sdk.py`:

```python
class StepType(IntEnum):
    """Defines the specific type of transaction/operation step."""
    kNormalFrom = 0                 # Standard transfer (Sender side)
    kNormalTo = 1                   # Cross-shard confirmation (Sender-side statistics)
    kConsensusRootElectShard = 2    # Shard/Root network election
    kConsensusRootTimeBlock = 3     # Time block creation
    kConsensusCreateGenesisAcount = 4 # Genesis account creation
    kConsensusLocalTos = 5          # Cross-shard confirmation (Receiver-side accumulation)
    kCreateContract = 6             # Contract deployment/creation
    kContractGasPrefund = 7         # Set contract call gas prefund
    kContractExcute = 8             # Execute contract call
    kRootCreateAddress = 9          # Root network address creation (internal use)
    kStatistic = 12                 # Statistical transaction
    kJoinElect = 13                 # Node election participation
    kCreateLibrary = 14             # Create public contract library (Library)
    kCross = 15                     # Cross-shard anti-loss block replenishment
    kRootCross = 16                 # Root network cross-shard replenishment
    kPoolStatisticTag = 17          # End tag for transaction pool statistics round
    kContractRefund = 18            # Contract call gas refund
```

## Common Operations and Correct Step Types

### 1. Create New User Address (Transfer Funds)

**✅ Correct:**
```python
tx_hash = w3.client.send_transaction_auto(
    funder_key,
    new_address,
    StepType.kNormalFrom,  # Standard transfer
    amount=10000000
)
```

**❌ Wrong:**
```python
tx_hash = w3.client.send_transaction_auto(
    funder_key,
    new_address,
    StepType.kRootCreateAddress,  # Wrong! This is for internal use
    amount=10000000
)
```

**Explanation:** To create a new user address, you simply send funds to it using a standard transfer (`kNormalFrom`). The `kRootCreateAddress` step type is used internally by the Root network for special address creation operations.

### 2. Deploy Contract

**✅ Correct:**
```python
contract = w3.shardora.contract(abi=abi, bytecode=bytecode, sender_address=user_addr).deploy({
    'from': user_addr,
    'salt': salt,
    'step': StepType.kCreateContract,  # Optional, this is the default
}, user_key)
```

**Note:** The `step` parameter defaults to `kCreateContract` in the deploy function, so you usually don't need to specify it explicitly.

### 3. Call Contract Function

**✅ Correct:**
```python
receipt = contract.functions.myFunction(arg1, arg2).transact(user_key)
```

**Note:** The SDK automatically uses `StepType.kContractExcute` for contract function calls.

### 4. Prefund Contract Gas

**✅ Correct:**
```python
receipt = contract.prefund(amount=1000000, private_key=user_key)
```

**Note:** The SDK automatically uses `StepType.kContractGasPrefund`.

### 5. Refund Gas from Contract

**✅ Correct:**
```python
receipt = contract.refund(private_key=user_key)
```

**Note:** The SDK automatically uses `StepType.kContractRefund`.

### 6. Standard Transfer Between Users

**✅ Correct:**
```python
receipt = w3.send_transaction({
    'to': recipient_address,
    'value': amount
}, sender_key)
```

**Note:** The SDK automatically uses `StepType.kNormalFrom`.

## Step Type Usage Matrix

| Operation | Step Type | Value | User Specifies? | Notes |
|-----------|-----------|-------|-----------------|-------|
| Create address (transfer) | `kNormalFrom` | 0 | ❌ Auto | Standard transfer to new address |
| Transfer between users | `kNormalFrom` | 0 | ❌ Auto | Standard transfer |
| Deploy contract | `kCreateContract` | 6 | ❌ Auto | Contract deployment |
| Call contract | `kContractExcute` | 8 | ❌ Auto | Execute contract function |
| Prefund contract | `kContractGasPrefund` | 7 | ❌ Auto | Set contract gas prefund |
| Refund from contract | `kContractRefund` | 18 | ❌ Auto | Refund gas from contract |
| Root address creation | `kRootCreateAddress` | 9 | ⚠️ Internal | Internal use only |
| Join election | `kJoinElect` | 13 | ✅ Manual | Node election participation |
| Create library | `kCreateLibrary` | 14 | ✅ Manual | Create public library |

## Common Mistakes

### Mistake 1: Using kRootCreateAddress for User Creation

❌ **Wrong:**
```python
# DON'T DO THIS
tx_hash = w3.client.send_transaction_auto(
    funder_key, new_address, 
    StepType.kRootCreateAddress,  # Wrong!
    amount=10000000
)
```

✅ **Correct:**
```python
# DO THIS
tx_hash = w3.client.send_transaction_auto(
    funder_key, new_address,
    StepType.kNormalFrom,  # Correct!
    amount=10000000
)
```

### Mistake 2: Manually Specifying Step for Contract Calls

❌ **Wrong:**
```python
# DON'T DO THIS - SDK handles it automatically
tx_hash = w3.client.send_transaction_auto(
    user_key, contract_address,
    StepType.kContractExcute,  # Unnecessary
    input_hex=encoded_call
)
```

✅ **Correct:**
```python
# DO THIS - Let SDK handle it
receipt = contract.functions.myFunction().transact(user_key)
```

### Mistake 3: Using Wrong Step for Contract Deployment

❌ **Wrong:**
```python
# DON'T DO THIS
tx_hash = w3.client.send_transaction_auto(
    user_key, contract_address,
    StepType.kNormalFrom,  # Wrong! This is for transfers
    contract_code=bytecode
)
```

✅ **Correct:**
```python
# DO THIS
contract = w3.shardora.contract(abi=abi, bytecode=bytecode).deploy({
    'from': user_addr,
    'salt': salt,
}, user_key)
```

## SDK Automatic Step Type Selection

The Shardora SDK automatically selects the correct step type based on the operation:

### ShardoraWeb3Mock.send_transaction()
```python
def send_transaction(self, tx_dict: dict, private_key: str) -> dict:
    tx_hash = self.client.send_transaction_auto(
        private_key, tx_dict['to'], 
        StepType.kNormalFrom,  # Automatically uses kNormalFrom
        amount=tx_dict.get('value', 0)
    )
    return self.client.wait_for_receipt(tx_hash)
```

### ShardoraContract.deploy()
```python
def deploy(self, transaction: dict, private_key: str) -> ShardoraContract:
    step = transaction.get('step', StepType.kCreateContract)  # Defaults to kCreateContract
    # ... deployment logic
```

### ShardoraMethod.transact()
```python
def transact(self, private_key: str, value: int = 0, prefund: int = 10**6) -> dict:
    tx_hash = self.contract.client.send_transaction_auto(
        private_key, 
        self.contract.address, 
        StepType.kContractExcute,  # Automatically uses kContractExcute
        amount=value, 
        input_hex=self.encoded_input, 
        prefund=prefund
    )
    # ... wait for receipt
```

### ShardoraContract.prefund()
```python
def prefund(self, amount: int, private_key: str) -> dict:
    tx_hash = self.client.send_transaction_auto(
        private_key, self.address, 
        StepType.kContractGasPrefund,  # Automatically uses kContractGasPrefund
        prefund=amount
    )
    return self.client.wait_for_receipt(tx_hash)
```

### ShardoraContract.refund()
```python
def refund(self, private_key: str) -> dict:
    tx_hash = self.client.send_transaction_auto(
        private_key, self.address, 
        StepType.kContractRefund,  # Automatically uses kContractRefund
    )
    return self.client.wait_for_receipt(tx_hash)
```

## Best Practices

1. **Let the SDK handle step types** - The SDK automatically selects the correct step type for most operations
2. **Use high-level functions** - Use `w3.send_transaction()`, `contract.deploy()`, `contract.functions.xxx().transact()` instead of low-level `send_transaction_auto()`
3. **Only specify step manually for special operations** - Like `kJoinElect` or `kCreateLibrary`
4. **Never use internal step types** - Like `kRootCreateAddress`, `kConsensusRootElectShard`, etc.

## Examples from Contract Chain Demo

### Creating User Addresses
```python
def create_and_wait_for_address(w3, funder_key, target_shard, target_pool, initial_balance=10000000):
    # Generate address
    private_key, address = generate_user_for_target_shard_pool(target_shard, target_pool)
    
    # Create address via standard transfer
    tx_hash = w3.client.send_transaction_auto(
        funder_key,
        address,
        StepType.kNormalFrom,  # ✅ Correct: Standard transfer
        amount=initial_balance
    )
    
    # Wait for confirmation
    # ...
    return private_key, address
```

### Deploying Contracts
```python
# Deploy ContractA
contract_a = w3.shardora.contract(abi=a_abi, bytecode=a_bin, sender_address=user1_addr).deploy({
    'from': user1_addr,
    'salt': salt_a,
    # step is automatically kCreateContract
}, user1_key)

# Deploy ContractB with constructor args
contract_b = w3.shardora.contract(abi=b_abi, bytecode=b_bin, sender_address=user2_addr).deploy({
    'from': user2_addr,
    'salt': salt_b,
    'args': [contract_a.address],  # Constructor argument
    # step is automatically kCreateContract
}, user2_key)
```

### Calling Contract Functions
```python
# Call ContractA.getValue()
value = contract_a.functions.getValue().call()

# Call ContractC.triggerChainUpdate()
receipt = contract_c.functions.triggerChainUpdate().transact(user3_key)
# step is automatically kContractExcute
```

## Troubleshooting

### Transaction Fails with "Invalid Step Type"
- Check that you're using the correct step type for the operation
- Verify you're not using internal step types like `kRootCreateAddress`
- Let the SDK handle step type selection when possible

### Address Creation Fails
- Make sure you're using `StepType.kNormalFrom` for transfers
- Verify the funder has sufficient balance
- Check that the recipient address is valid

### Contract Deployment Fails
- Ensure you're using the `contract.deploy()` method
- Don't manually specify `StepType.kCreateContract` unless needed
- Verify the bytecode is valid

## Related Documents

- `CONTRACT_CHAIN_SAME_SHARD_POOL_DEMO.md` - Contract chain demo
- `CONTRACT_CHAIN_QUERY_LOGIC.md` - Query logic documentation
- `shardora_sdk.py` - SDK implementation with step type handling
