#pragma once

#include "common/encode.h"
#include "common/hash.h"
#include "common/lof.h"
#include "common/node_members.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "db/db.h"
#include "protos/pools.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"
#include "zjcvm/zjc_host.h"

namespace zjchain {

namespace pools {

static const uint32_t kBftStartDeltaTime = 1000000u;
static const uint32_t kTxPoolTimeoutUs = 1800u * 1000u * 1000u;
static const uint32_t kTxStorageKeyMaxSize = 12u;
static const uint32_t kMaxToTxsCount = 10000u;
static const uint32_t kLeafMaxHeightCount = 1024u * 1024u;// 1024u * 1024u;  // each merkle block 1M
static const uint32_t kEachHeightTreeMaxByteSize = kLeafMaxHeightCount * 2u;  // each merkle block 1M
static const uint32_t kBranchMaxCount = kLeafMaxHeightCount / 64u;
static const uint32_t kHeightLevelItemMaxCount = 2 * kBranchMaxCount - 1;
static const uint64_t kLevelNodeValidHeights = 0xFFFFFFFFFFFFFFFFlu;
static const uint32_t kStatisticMaxCount = 3u;
static const uint32_t kWaitingElectNodesMaxCount = 256u;

enum PoolsErrorCode {
    kPoolsSuccess = 0,
    kPoolsError = 1,
    kPoolsTxAdded = 2,
};

class TxItem {
public:
    virtual ~TxItem() {}
    TxItem(const transport::MessagePtr& msg)
            : msg_ptr(msg),
            prev_consensus_tm_us(0),
            tx_hash(msg->header.tx_proto().gid()),
            unnique_tx_hash(msg_ptr->msg_hash),
            gid(msg->header.tx_proto().gid()),
            gas_price(msg->header.tx_proto().gas_price()),
            in_consensus(false) {
        uint64_t now_tm = common::TimeUtils::TimestampUs();
        time_valid = now_tm + kBftStartDeltaTime;
#ifdef ZJC_UNITTEST
        time_valid = 0;
#endif // ZJC_UNITTEST
        timeout = now_tm + kTxPoolTimeoutUs;
        remove_timeout = timeout + kTxPoolTimeoutUs;
        if (msg->header.tx_proto().has_step()) {
            step = msg->header.tx_proto().step();
        }

        auto prio = common::ShiftUint64(now_tm);
        prio_key = std::string((char*)&prio, sizeof(prio)) + gid;
    }

    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) = 0;
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        block::protobuf::BlockTx* block_tx) = 0;

    transport::MessagePtr msg_ptr;
    uint64_t prev_consensus_tm_us;
    uint64_t timeout;
    uint64_t remove_timeout;
    uint64_t time_valid{ 0 };
    const uint64_t& gas_price;
    int32_t step = pools::protobuf::kNormalFrom;
    const std::string& tx_hash;
    const std::string& unnique_tx_hash;
    const std::string& gid;
    std::string prio_key;
    bool in_consensus;
};

typedef std::shared_ptr<TxItem> TxItemPtr;
typedef std::function<TxItemPtr(const transport::MessagePtr& msg_ptr)> CreateConsensusItemFunction;


struct StatisticElectItem {
    StatisticElectItem() : elect_height(0) {
        memset(succ_tx_count, 0, sizeof(succ_tx_count));
    }

    void Clear() {
        elect_height = 0;
        memset(succ_tx_count, 0, sizeof(succ_tx_count));
        leader_lof_map.clear();
    }

    uint64_t elect_height{ 0 };
    uint32_t succ_tx_count[common::kEachShardMaxNodeCount];
    std::unordered_map<int32_t, std::shared_ptr<common::Point>> leader_lof_map;
    std::mutex leader_lof_map_mutex;
};

typedef std::shared_ptr<StatisticElectItem> StatisticElectItemPtr;

struct StatisticItem {
    StatisticItem() {
        for (uint32_t i = 0; i < kStatisticMaxCount; ++i) {
            elect_items[i] = std::make_shared<StatisticElectItem>();
        }
    }

    void Clear() {
        for (uint32_t i = 0; i < kStatisticMaxCount; ++i) {
            elect_items[i]->Clear();
        }

        all_tx_count = 0;
        tmblock_height = 0;
        added_height.clear();
    }

    StatisticElectItemPtr elect_items[kStatisticMaxCount];
    uint32_t all_tx_count{ 0 };
    std::unordered_set<uint64_t> added_height;
    uint64_t tmblock_height{ 0 };
};

struct ElectItem {
    uint64_t height;
    common::MembersPtr members;
};

struct StatisticMemberInfoItem {
    StatisticMemberInfoItem(uint32_t midx, uint32_t lidx)
        : tx_count(0), member_index(midx), leader_index(lidx) {}
    uint32_t tx_count;
    uint32_t member_index;
    uint32_t leader_index;
};

struct HeightStatisticInfo {
    std::unordered_map<std::string, StatisticMemberInfoItem> node_tx_count_map;
    std::unordered_map<std::string, uint64_t> node_stoke_map;
    uint64_t elect_height;
    uint64_t all_gas_amount;
};

struct RootStatisticItem {
    uint32_t history_tx_count;
    uint32_t tmp_tx_count;
    uint32_t epoch_tx_count;
};

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
