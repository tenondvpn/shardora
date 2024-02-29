#pragma once

#include <functional>
#include <memory>

#include "evmc/evmc.h"

#include "common/bloom_filter.h"
#include "common/log.h"
#include "common/utils.h"
#include "pools/tx_utils.h"
#include "protos/block.pb.h"

namespace zjchain {

namespace consensus {

enum ConsensusErrorCode {
    kConsensusSuccess = 0,
    kConsensusError = 1,
    kConsensusAdded = 2,
    kConsensusNotExists = 4,
    kConsensusTxAdded = 5,
    kConsensusNoNewTxs = 6,
    kConsensusInvalidPackage = 7,
    kConsensusTxNotExists = 8,
    kConsensusAccountNotExists = 9,
    kConsensusAccountBalanceError = 10,
    kConsensusAccountExists = 11,
    kConsensusBlockHashError = 12,
    kConsensusBlockHeightError = 13,
    kConsensusPoolIndexError = 14,
    kConsensusBlockNotExists = 15,
    kConsensusBlockPreHashError = 16,
    kConsensusNetwokInvalid = 17,
    kConsensusLeaderInfoInvalid = 18,
    kConsensusExecuteContractFailed = 19,
    kConsensusGasUsedNotEqualToLeaderError = 20,
    kConsensusUserSetGasLimitError = 21,
    kConsensusCreateContractKeyError = 22,
    kConsensusContractAddressLocked = 23,
    kConsensusContractBytesCodeError = 24,
    kConsensusTimeBlockHeightError = 25,
    kConsensusElectBlockHeightError = 26,
    kConsensusLeaderTxInfoInvalid = 27,
    kConsensusVssRandomNotMatch = 28,
    kConsensusWaiting = 29,
    kConsensusOutOfGas = 30,
    kConsensusRevert = 31,
    kConsensusInvalidInstruction = 32,
    kConsensusUndefinedInstruction = 33,
    kConsensusStackOverflow = 34,
    kConsensusStackUnderflow = 35,
    kConsensusBadJumpDestination = 36,
    kConsensusInvalidMemoryAccess = 37,
    kConsensusCallDepthExceeded = 38,
    kConsensusStaticModeViolation = 39,
    kConsensusPrecompileFailure = 40,
    kConsensusContractValidationFailure = 41,
    kConsensusArgumentOutOfRange = 42,
    kConsensusWasmRnreachableInstruction = 43,
    kConsensusWasmTrap = 44,
    kConsensusInsufficientBalance = 45,
    kConsensusInternalError = 46,
    kConsensusRejected = 47,
    kConsensusOutOfMemory = 48,
    kConsensusOutOfPrepayment = 49,
    kConsensusElectNodeExists = 50,
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

static const uint32_t kMaxTxCount = 1024u;
static const uint32_t kBitcountWithItemCount = 20u;  // m/n, k = 8, error ratio = 0.000009
static const uint32_t kHashCount = 6u;  // k
static const uint32_t kDirectTxCount = kBitcountWithItemCount * 8 / 32;
// gas consume
static const uint64_t kTransferGas = 1000llu;
static const uint64_t kJoinElectGas = 10000llu;
static const uint64_t kCallContractDefaultUseGas = 1000llu;
static const uint64_t kCreateLibraryDefaultUseGas = 100000llu;
static const uint64_t kCreateContractDefaultUseGas = 100000llu;
static const uint64_t kKeyValueStorageEachBytes = 10llu;

static int32_t EvmcStatusToZbftStatus(evmc_status_code status_code) {
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
        tx_type(pools::protobuf::kNormalFrom) {}
    std::map<std::string, pools::TxItemPtr> txs;
    std::unordered_map<std::string, uint32_t> all_hash_count;
    std::string max_txs_hash;
    uint32_t max_txs_hash_count;
    uint32_t pool_index;
    uint8_t thread_index;
    pools::protobuf::StepType tx_type;
};

};  // namespace consensus

};  // namespace zjchain
