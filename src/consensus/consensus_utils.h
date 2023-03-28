#pragma once

#include <functional>
#include <memory>

#include "common/utils.h"
#include "common/log.h"
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
};

enum BftStatus {
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
};

enum BftRole {
    kConsensusRootCongress = 0,
    kConsensusShard = 1,
};

enum BftLeaderCheckStatus {
    kConsensusWaitingBackup = 0,
    kConsensusOppose = 1,
    kConsensusAgree = 2,
    kConsensusHandled = 3,
    kConsensusReChallenge = 4,
};

static const uint32_t kMaxTxCount = 256u;
static const uint32_t kBitcountWithItemCount = 20u;  // m/n, k = 8, error ratio = 0.000009
static const uint32_t kHashCount = 6u;  // k
static const uint32_t kDirectTxCount = kBitcountWithItemCount * 8 / 32;
// gas consume
static const uint64_t kTransferGas = 1000llu;
static const uint64_t kCallContractDefaultUseGas = 10000llu;
static const uint64_t kKeyValueStorageEachBytes = 10llu;

typedef std::function<int(const std::shared_ptr<block::protobuf::Block>& block)> BlockCallback;

struct WaitingTxsItem {
    WaitingTxsItem()
        : bloom_filter(nullptr),
        max_txs_hash_count(0),
        tx_type(pools::protobuf::kNormalFrom) {}
    std::map<std::string, pools::TxItemPtr> txs;
    std::shared_ptr<common::BloomFilter> bloom_filter;
    std::unordered_map<std::string, uint32_t> all_hash_count;
    std::string max_txs_hash;
    uint32_t max_txs_hash_count;
    uint32_t pool_index;
    uint8_t thread_index;
    pools::protobuf::StepType tx_type;
};

};  // namespace consensus

};  // namespace zjchain
