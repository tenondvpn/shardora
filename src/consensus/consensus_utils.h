#pragma once

#include <functional>
#include <memory>

#include "common/utils.h"
#include "common/log.h"
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
};

static const uint32_t kMaxTxCount = 64u;
static const uint32_t kBitcountWithItemCount = 20u;  // m/n, k = 8, error ratio = 0.000009
static const uint32_t kHashCount = 6u;  // k
static const uint32_t kDirectTxCount = kBitcountWithItemCount * 8 / 32;

typedef std::function<int(const std::shared_ptr<block::protobuf::Block>& block)> BlockCallback;

};  // namespace consensus

};  // namespace zjchain
