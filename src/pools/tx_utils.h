#pragma once

#include <deque>

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

static const uint64_t kBftStartDeltaTime = 3000000lu; // 预留交易生效时间，避免部分节点找不到交易（但会增大交易 latency）
static const uint32_t kTxPoolTimeoutUs = 10u * 1000u * 1000u;
static const uint32_t kTxStorageKeyMaxSize = 12u;
static const uint32_t kMaxToTxsCount = 10000u;
static const uint32_t kLeafMaxHeightCount = 1024u * 1024u;// 1024u * 1024u;  // each merkle block 1M
static const uint32_t kEachHeightTreeMaxByteSize = kLeafMaxHeightCount * 2u;  // each merkle block 1M
static const uint32_t kBranchMaxCount = kLeafMaxHeightCount / 64u;
static const uint32_t kHeightLevelItemMaxCount = 2 * kBranchMaxCount - 1;
static const uint64_t kLevelNodeValidHeights = 0xFFFFFFFFFFFFFFFFlu;
static const uint32_t kStatisticMaxCount = 3u;
static const uint32_t kWaitingElectNodesMaxCount = 256u;
static const uint64_t kCheckLeaderLofPeriod = 3000lu;
static const uint64_t kCaculateLeaderLofPeriod = 24000lu;
static const uint32_t kLeaderLofFactorCount = kCaculateLeaderLofPeriod / kCheckLeaderLofPeriod * 2 / 3;

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
        zjcvm::ZjchainHost& zjc_host,
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
typedef std::function<void(uint8_t thread_idx, const std::deque<std::shared_ptr<std::vector<std::pair<uint32_t, uint32_t>>>>& invalid_pools)> RotationLeaderCallback;

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
    uint32_t tx_count;
    uint32_t member_index;
    uint32_t leader_index;
};

struct CrossStatisticItem {
    CrossStatisticItem() : des_net(0), cross_ptr(nullptr) {}
    CrossStatisticItem(uint32_t shard) : des_net(shard), cross_ptr(nullptr) {}
    uint32_t des_net;
    std::shared_ptr<pools::protobuf::CrossShardStatistic> cross_ptr;
};

struct HeightStatisticInfo {
    HeightStatisticInfo() : elect_height(0), all_gas_amount(0), all_gas_for_root(0) {}
    std::unordered_map<std::string, StatisticMemberInfoItem> node_tx_count_map;
    std::unordered_map<std::string, uint64_t> node_stake_map;
    std::unordered_map<std::string, uint32_t> node_shard_map;
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, uint32_t>> pool_cross_shard_heights;
    uint64_t elect_height;
    uint64_t all_gas_amount;
    uint64_t all_gas_for_root;
};

struct RootStatisticItem {
    uint32_t history_tx_count;
    uint32_t tmp_tx_count;
    uint32_t epoch_tx_count;
};

struct CrossShardItem {
    uint32_t pool;
    uint32_t des_shard;
};

struct InvalidGidItem {
    InvalidGidItem() : max_pool_index_count(0), max_pool_height_count(0) {}
    std::set<std::string> checked_members;
    std::string gid;
    std::map<uint32_t, uint32_t> pool_index;
    std::map<uint64_t, uint32_t> heights;
    std::map<std::string, uint32_t> precommit_hashs;
    std::map<std::string, uint32_t> prepare_hashs;
    uint32_t max_pool_index_count;
    uint32_t max_pool_index;
    uint32_t max_pool_height_count;
    uint32_t max_pool_height;
    std::string max_precommit_hash;
};

struct PoolsCountPrioItem {
    PoolsCountPrioItem(uint32_t p, uint32_t c) : count(c), pool_index(p) {}
    uint32_t count;
    uint32_t pool_index;
    bool operator<(const PoolsCountPrioItem& a) const {
        return a.count < count;
    }
};

struct PoolsTmPrioItem {
    PoolsTmPrioItem(uint32_t p, uint64_t t) : max_timestamp(t), pool_index(p) {}
    uint64_t max_timestamp;
    uint32_t pool_index;
    bool operator<(const PoolsTmPrioItem& a) const {
        return max_timestamp < a.max_timestamp;
    }
};

struct StaticCreditInfo {
    StaticCreditInfo() : elect_height(common::kInvalidUint64) {
        memset(leaders_gas_sum, 0, sizeof(leaders_gas_sum));
        memset(max_height, 0, sizeof(max_height));
    }

    uint64_t leaders_gas_sum[common::kEachShardMaxNodeCount];
    uint64_t elect_height;
    uint64_t max_height[common::kInvalidPoolIndex];
    std::unordered_set<uint64_t> heights;
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

    if (tx_info.has_contract_code()) {
        message.append(tx_info.contract_code());
    }

    if (tx_info.has_contract_input()) {
        message.append(tx_info.contract_input());
    }

    if (tx_info.has_contract_prepayment()) {
        uint64_t prepay = tx_info.contract_prepayment();
        message.append(std::string((char*)&prepay, sizeof(prepay)));
    }

    if (tx_info.has_key()) {
        message.append(tx_info.key());
        if (tx_info.has_value()) {
            message.append(tx_info.value());
        }
    }

    ZJC_DEBUG("message: %s", common::Encode::HexEncode(message).c_str());
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
