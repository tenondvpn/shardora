#pragma once

#include "common/hash.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "protos/pools.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace pools {

static const uint32_t kBftStartDeltaTime = 300000u;
static const uint32_t kTxPoolTimeoutUs = 30u * 1000u * 1000u * 60u;
static const uint32_t kTxStorageKeyMaxSize = 12u;
static const uint32_t kMaxToTxsCount = 10000u;

enum PoolsErrorCode {
    kPoolsSuccess = 0,
    kPoolsError = 1,
    kPoolsTxAdded = 2,
};

class TxItem {
public:
    virtual ~TxItem() {}
    TxItem(transport::MessagePtr& msg) : msg_ptr(msg) {
        time_valid = common::TimeUtils::TimestampUs() + kBftStartDeltaTime;
#ifdef ZJC_UNITTEST
        time_valid = 0;
#endif // ZJC_UNITTEST
        timeout = common::TimeUtils::TimestampUs() + kTxPoolTimeoutUs;
        remove_timeout = timeout + kTxPoolTimeoutUs;
        gas_price = msg->header.tx_proto().gas_price();
        if (msg->header.tx_proto().has_step()) {
            step = msg->header.tx_proto().step();
        }

        tx_hash = common::Hash::keccak256(
            msg->header.tx_proto().gid() + std::to_string(step) + msg->msg_hash);
    }

    virtual int HandleTx(
        uint8_t thread_idx,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) = 0;
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) = 0;

    transport::MessagePtr msg_ptr;
    uint64_t timeout;
    uint64_t remove_timeout;
    uint64_t time_valid{ 0 };
    uint64_t gas_price{ 0 };
    int32_t step = pools::protobuf::kNormalFrom;
    std::string from_addr;
    std::string tx_hash;
};

typedef std::shared_ptr<TxItem> TxItemPtr;
typedef std::function<TxItemPtr(transport::MessagePtr& msg_ptr)> CreateConsensusItemFunction;

static inline std::string GetTxMessageHash(const pools::protobuf::TxMessage& tx_info) {
    std::string message;
    message.reserve(tx_info.ByteSizeLong());
    message.append(tx_info.gid());
    message.append(tx_info.pubkey());
    message.append(tx_info.to());
    uint64_t amount = tx_info.amount();
    message.append(std::string((char*)&amount, sizeof(amount)));
    uint64_t gas_limit = tx_info.gas_limit();
    message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = tx_info.gas_price();
    message.append(std::string((char*)&gas_price, sizeof(gas_price)));
    if (tx_info.has_step()) {
        uint64_t step = tx_info.step();
        message.append(std::string((char*)&step, sizeof(step)));
    }

    if (tx_info.has_key()) {
        message.append(tx_info.key());
        if (tx_info.has_value()) {
            message.append(tx_info.value());
        }
    }

//     ZJC_DEBUG("message: %s", common::Encode::HexEncode(message).c_str());
    return common::Hash::keccak256(message);
}

static std::string GetTxMessageHashByJoin(const pools::protobuf::TxMessage& tx_info) {
    std::string message;
    message.reserve(tx_info.GetCachedSize() * 2);
    message.append(common::Encode::HexEncode(tx_info.gid()));
    message.append(1, '-');
    message.append(common::Encode::HexEncode(tx_info.pubkey()));
    message.append(1, '-');
    message.append(common::Encode::HexEncode(tx_info.to()));
    message.append(1, '-');
    if (tx_info.has_key()) {
        message.append(common::Encode::HexEncode(tx_info.key()));
        message.append(1, '-');
        if (tx_info.has_value()) {
            message.append(common::Encode::HexEncode(tx_info.value()));
            message.append(1, '-');
        }
    }

    message.append(std::to_string(tx_info.amount()));
    message.append(1, '-');
    message.append(std::to_string(tx_info.gas_limit()));
    message.append(1, '-');
    message.append(std::to_string(tx_info.gas_price()));
    ZJC_DEBUG("src message: %s", message.c_str());
    return common::Hash::keccak256(message);
}

};  // namespace pools

};  // namespace zjchain
