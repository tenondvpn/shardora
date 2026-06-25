#pragma once

#include <functional>
#include <memory>
#include <string>

#include "evmc/evmc.h"

#include "common/bloom_filter.h"
#include "common/log.h"
#include "common/utils.h"
#include "pools/tx_utils.h"
#include "protos/view_block.pb.h"

namespace shardora {

namespace consensus {

// hash128(gid + from + to + amount + type + attrs(k:v))
// std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info);
// // prehash + network_id + height + random + elect version + txes's hash
// std::string GetViewBlockHash(const view_block::protobuf::ViewBlockItem& view_block);
typedef std::function<void(
    std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
    db::DbWriteBatch& db_batch)> BlockCacheCallback;

enum ConsensusErrorCode : int32_t {
    kConsensusSuccess = 0,
    kConsensusError = 5001,
    kConsensusAdded = 5002,
    kConsensusNotExists = 5004,
    kConsensusTxAdded = 5005,
    kConsensusNoNewTxs = 5006,
    kConsensusInvalidPackage = 5007,
    kConsensusTxNotExists = 5008,
    kConsensusAccountNotExists = 5009,
    kConsensusAccountBalanceError = 5010,
    kConsensusAccountExists = 5011,
    kConsensusBlockHashError = 5012,
    kConsensusBlockHeightError = 5013,
    kConsensusPoolIndexError = 5014,
    kConsensusBlockNotExists = 5015,
    kConsensusBlockPreHashError = 5016,
    kConsensusNetwokInvalid = 5017,
    kConsensusLeaderInfoInvalid = 5018,
    kConsensusExecuteContractFailed = 5019,
    kConsensusGasUsedNotEqualToLeaderError = 5020,
    kConsensusUserSetGasLimitError = 5021,
    kConsensusCreateContractKeyError = 5022,
    kConsensusContractAddressLocked = 5023,
    kConsensusContractBytesCodeError = 5024,
    kConsensusTimeBlockHeightError = 5025,
    kConsensusElectBlockHeightError = 5026,
    kConsensusLeaderTxInfoInvalid = 5027,
    kConsensusVssRandomNotMatch = 5028,
    kConsensusWaiting = 5029,
    kConsensusOutOfGas = 5030,
    kConsensusRevert = 5031,
    kConsensusInvalidInstruction = 5032,
    kConsensusUndefinedInstruction = 5033,
    kConsensusStackOverflow = 5034,
    kConsensusStackUnderflow = 5035,
    kConsensusBadJumpDestination = 5036,
    kConsensusInvalidMemoryAccess = 5037,
    kConsensusCallDepthExceeded = 5038,
    kConsensusStaticModeViolation = 5039,
    kConsensusPrecompileFailure = 5040,
    kConsensusContractValidationFailure = 5041,
    kConsensusArgumentOutOfRange = 5042,
    kConsensusWasmRnreachableInstruction = 5043,
    kConsensusWasmTrap = 5044,
    kConsensusInsufficientBalance = 5045,
    kConsensusInternalError = 5046,
    kConsensusRejected = 5047,
    kConsensusOutOfMemory = 5048,
    kConsensusOutOfPrefund = 5049,
    kConsensusElectNodeExists = 5050,
    kConsensusNonceInvalid = 5051,
    kConsensusJoinElectThreashTInvalid = 5052,
    kConsensusContractDestructed = 5053,
    kConsensusContractNotExists = 5054,
    kConsensusContractPrefundNotExists = 5055,
};

enum BftStatus : int32_t {
    kConsensusInit = 0,
    kConsensusPrepare = 1,
    kConsensusPreCommit = 2,
    kConsensusCommit = 3,
    kConsensusCommited = 4,
    kConsensusToTxInit = 5,
    kConsensusRootBlock = 6,
    kConsensusCallContract = 7,
    kConsensusStepTimeout = 8,
    kConsensusSyncBlock = 9,
    kConsensusFailed = 10,
    kConsensusWaitingBackup = 11,
    kConsensusOppose = 12,
    kConsensusAgree = 13,
    kConsensusHandled = 14,
    kConsensusReChallenge = 15,
    kConsensusLeaderWaitingBlock = 16,
};

enum BftRole {
    kConsensusRootCongress = 0,
    kConsensusShard = 1,
};

static const uint32_t kSyncToLeaderTxCount = common::kMaxTxCount; // consensus can be slow if it's large
static const uint32_t kBitcountWithItemCount = 20u;  // m/n, k = 8, error ratio = 0.000009
static const uint32_t kHashCount = 6u;  // k
static const uint32_t kDirectTxCount = kBitcountWithItemCount * 8 / 32;
// gas consume — aligned with Ethereum Yellow Paper / EIP-2028
// Base transaction gas (EIP-2028): 21000
static const uint64_t kTransferGas = 21000llu;
// Join-elect is a special internal tx; keep a reasonable overhead
static const uint64_t kJoinElectGas = 21000llu;
// Contract call intrinsic gas (same as a plain tx): 21000
static const uint64_t kCallContractDefaultUseGas = 21000llu;
// Library / contract creation intrinsic gas (CREATE opcode base): 53000
static const uint64_t kCreateLibraryDefaultUseGas = 53000llu;
static const uint64_t kCreateContractDefaultUseGas = 53000llu;
// Maximum gas allowed in one packed block, aligned with Ethereum mainnet.
static const uint64_t kBlockMaxGasLimit = 60000000000000000llu;
// Calldata gas per byte: non-zero = 16, zero = 4 (EIP-2028)
static const uint64_t kCalldataNonZeroByteGas = 16llu;
static const uint64_t kCalldataZeroByteGas = 4llu;
// Key-value storage gas — aligned with ETH EIP-2200 SSTORE pricing.
// New slot write (zero → non-zero): 20000 gas per 32-byte slot.
// Dirty slot update (non-zero → non-zero): 2900 gas per 32-byte slot.
static const uint64_t kSstoreNewSlotGas    = 20000llu;
static const uint64_t kSstoreDirtySlotGas  = 2900llu;
static const uint64_t kStorageSlotBytes    = 32llu;
// Legacy per-byte constant kept for any remaining callers during migration.
static const uint64_t kKeyValueStorageEachBytes = 10llu;

// Calculate KV storage gas: round byte length up to 32-byte slots, then
// apply SSTORE pricing.  Use is_new=true for first write, false for update.
inline static uint64_t CalcKvStorageGas(size_t key_bytes, size_t value_bytes,
                                         bool is_new = true) {
    size_t total = key_bytes + value_bytes;
    uint64_t slots = (static_cast<uint64_t>(total) + kStorageSlotBytes - 1) / kStorageSlotBytes;
    if (slots == 0) slots = 1;
    return slots * (is_new ? kSstoreNewSlotGas : kSstoreDirtySlotGas);
}

// Calculate calldata gas cost following EIP-2028 rules.
inline static uint64_t CalcCalldataGas(const std::string& data) {
    uint64_t gas = 0;
    for (unsigned char c : data) {
        gas += (c == 0) ? kCalldataZeroByteGas : kCalldataNonZeroByteGas;
    }
    return gas;
}

inline static std::string SafeEvmcOutput(const evmc_result& res) {
    if (res.output_data == nullptr || res.output_size == 0) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(res.output_data), res.output_size);
}

inline static bool CanAddBlockGas(uint64_t current_gas, uint64_t gas_to_add) {
    return current_gas <= kBlockMaxGasLimit &&
           gas_to_add <= kBlockMaxGasLimit - current_gas;
}

inline static int32_t EvmcStatusToZbftStatus(evmc_status_code status_code) {
    switch (status_code) {
    case EVMC_SUCCESS:
        return kConsensusSuccess;
    case EVMC_FAILURE:
        return kConsensusError;
    case EVMC_REVERT:
        return kConsensusRevert;
    case EVMC_OUT_OF_GAS:
        return kConsensusOutOfGas;
    case EVMC_INVALID_INSTRUCTION:
        return kConsensusInvalidInstruction;
    case EVMC_UNDEFINED_INSTRUCTION:
        return kConsensusUndefinedInstruction;
    case EVMC_STACK_OVERFLOW:
        return kConsensusStackOverflow;
    case EVMC_STACK_UNDERFLOW:
        return kConsensusStackUnderflow;
    case EVMC_BAD_JUMP_DESTINATION:
        return kConsensusBadJumpDestination;
    case EVMC_INVALID_MEMORY_ACCESS:
        return kConsensusInvalidMemoryAccess;
    case EVMC_CALL_DEPTH_EXCEEDED:
        return kConsensusCallDepthExceeded;
    case EVMC_STATIC_MODE_VIOLATION:
        return kConsensusStaticModeViolation;
    case EVMC_PRECOMPILE_FAILURE:
        return kConsensusPrecompileFailure;
    case EVMC_CONTRACT_VALIDATION_FAILURE:
        return kConsensusContractValidationFailure;
    case EVMC_ARGUMENT_OUT_OF_RANGE:
        return kConsensusArgumentOutOfRange;
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
        return kConsensusWasmRnreachableInstruction;
    case EVMC_WASM_TRAP:
        return kConsensusWasmTrap;
    case EVMC_INSUFFICIENT_BALANCE:
        return kConsensusInsufficientBalance;
    case EVMC_INTERNAL_ERROR:
        return kConsensusInternalError;
    case EVMC_REJECTED:
        return kConsensusRejected;
    case EVMC_OUT_OF_MEMORY:
        return kConsensusOutOfMemory;
    default:
        return kConsensusError;
    }
}

typedef std::function<int(const std::shared_ptr<block::protobuf::Block>& block)> BlockCallback;

struct WaitingTxsItem {
    WaitingTxsItem()
        : max_txs_hash_count(0),
        tx_type(pools::protobuf::kNormalFrom) {
        }
    std::vector<pools::TxItemPtr> txs;
    std::unordered_map<std::string, std::string> kvs;
    std::unordered_map<std::string, uint32_t> all_hash_count;
    std::string max_txs_hash;
    uint32_t max_txs_hash_count;
    uint32_t pool_index;
    pools::protobuf::StepType tx_type;
};

};  // namespace consensus

};  // namespace shardora
