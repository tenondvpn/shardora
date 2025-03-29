#pragma once

#include <deque>
#include <protos/elect.pb.h>

#include "common/encode.h"
#include "common/hash.h"
#include "common/lof.h"
#include "common/node_members.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "db/db.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "network/network_utils.h"
#include "security/security.h"
#include "transport/transport_utils.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace pools {

static const uint64_t kBftStartDeltaTime = 10000000lu; // 预留交易生效时间，避免部分节点找不到交易（但会增大交易 latency）
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
    TxItem(const transport::MessagePtr& msgp, int32_t tx_info_idx, protos::AddressInfoPtr& addr_info)
            : prev_consensus_tm_us(0),
            address_info(addr_info),
            is_consensus_add_tx(false),
            tx_info_index(tx_info_idx) {
        msg_ptr = msgp;
        tx_info = nullptr;
        if (tx_info_index < 0) {
            tx_info = msg_ptr->header.mutable_tx_proto();
        } else {
            if (msg_ptr->header.hotstuff().has_pre_reset_timer_msg() &&
                    tx_info_idx < msg_ptr->header.hotstuff().pre_reset_timer_msg().txs_size()) {
                tx_info = msg_ptr->header.mutable_hotstuff()->mutable_pre_reset_timer_msg()->mutable_txs(tx_info_idx);
            } else if (msg_ptr->header.hotstuff().pro_msg().has_tx_propose()) {
                auto& propose_msg = msg_ptr->header.hotstuff().pro_msg().tx_propose();
                if (tx_info_idx < propose_msg.txs_size()) {
                    tx_info = msg_ptr->header.mutable_hotstuff()->mutable_pro_msg()->mutable_tx_propose()->mutable_txs(tx_info_idx);
                }
            } else if (msg_ptr->header.hotstuff().has_vote_msg()) {
                auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
                if (tx_info_idx < vote_msg.txs_size()) {
                    tx_info = msg_ptr->header.mutable_hotstuff()->mutable_vote_msg()->mutable_txs(tx_info_idx);
                }
            } else {
                assert(false)
;            }
        }

        if (tx_info == nullptr) {
            assert(false);
            return;
        }

        uint64_t now_tm = common::TimeUtils::TimestampUs();
        time_valid = now_tm + kBftStartDeltaTime;
#ifdef ZJC_UNITTEST
        time_valid = 0;
#endif // ZJC_UNITTEST
        remove_timeout = now_tm + kTxPoolTimeoutUs;
        auto prio = common::ShiftUint64(tx_info->gas_price());
        prio_key = std::string((char*)&prio, sizeof(prio)) + tx_info->gid();
    }

    TxItem() {
        uint64_t now_tm = common::TimeUtils::TimestampUs();
        time_valid = now_tm + kBftStartDeltaTime;
#ifdef ZJC_UNITTEST
        time_valid = 0;
#endif // ZJC_UNITTEST
        remove_timeout = now_tm + kTxPoolTimeoutUs;
        auto prio = common::ShiftUint64(tx_info->gas_price());
        prio_key = std::string((char*)&prio, sizeof(prio)) + tx_info->gid();
    }

    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) = 0;
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) = 0;

    uint64_t prev_consensus_tm_us;
    uint64_t remove_timeout;
    uint64_t time_valid{ 0 };
    std::string unique_tx_hash;
    std::string prio_key;
    pools::protobuf::TxMessage reload_tx_info;
    pools::protobuf::TxMessage* tx_info;
    transport::MessagePtr msg_ptr;
    protos::AddressInfoPtr address_info;
    bool is_consensus_add_tx;
    int32_t tx_info_index;
};

typedef std::shared_ptr<TxItem> TxItemPtr;
typedef std::function<TxItemPtr(const transport::MessagePtr& msg_ptr)> CreateConsensusItemFunction;
typedef std::function<bool(const std::string& gid)> CheckGidValidFunction;

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
    StatisticMemberInfoItem() : tx_count(0), member_index(0), leader_index(0), gas_sum(0), max_height(0) {}
    uint32_t tx_count = 0;
    uint32_t member_index = 0;
    uint32_t leader_index = 0;
    uint64_t gas_sum = 0;
    uint64_t max_height = 0;
};

struct AccoutPoceInfoItem {
    uint64_t consensus_gap; // 边缘化程度 P
    uint64_t credit;
};

struct CrossStatisticItem {
    CrossStatisticItem() : des_net(0), cross_ptr(nullptr) {}
    CrossStatisticItem(uint32_t shard) : des_net(shard), cross_ptr(nullptr) {}
    uint32_t des_net;
    std::shared_ptr<pools::protobuf::CrossShardStatistic> cross_ptr;
};

struct ElectNodeStatisticInfo {
    ElectNodeStatisticInfo() : all_gas_amount(0), 
        all_gas_for_root(0) {}
    std::map<std::string, StatisticMemberInfoItem> node_tx_count_map;
    std::map<std::string, uint64_t> node_stoke_map;
    std::map<std::string, uint32_t> node_shard_map;
    std::map<std::string, std::string> node_pubkey_map;
    uint64_t all_gas_amount;
    uint64_t all_gas_for_root;
};

struct HeightStatisticInfo {
    HeightStatisticInfo() : tm_height(0), max_height(0) {}
    uint64_t tm_height;
    uint64_t max_height;
    std::map<uint64_t, std::shared_ptr<ElectNodeStatisticInfo>> elect_node_info_map;
};

struct PoolBlocksInfo {
    PoolBlocksInfo() : latest_consensus_height_(0) {}
    std::map<uint64_t, std::shared_ptr<view_block::protobuf::ViewBlockItem>> blocks;
    uint64_t latest_consensus_height_;
};

struct PoolStatisticItem {
    uint64_t min_height;
    uint64_t max_height;
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

struct CrossItem {
    uint32_t src_shard;
    uint32_t src_pool;
    uint64_t height;
};

static inline bool operator==(const struct CrossItem & X,const struct CrossItem & Y) {
    return (Y.src_shard==X.src_shard) && (Y.src_pool==X.src_pool)  && (Y.height==X.height);
}


struct CrossItemRecordHash {
    size_t operator()(const struct CrossItem& item) const {
        char data[sizeof(CrossItem)];
        uint32_t* u32_arr = (uint32_t*)data;
        u32_arr[0] = item.src_shard;
        u32_arr[1] = item.src_pool;
        u32_arr[2] = static_cast<uint32_t>(item.height && 0xFFFFFFFFu);
        u32_arr[3] = static_cast<uint32_t>((item.height >> 32) && 0xFFFFFFFFu);
        return std::hash<std::string>()(data);
    }
};

struct StatisticInfoItem {
    StatisticInfoItem() 
        : all_gas_amount(0), 
        root_all_gas_amount(0), 
        statistic_min_height(0), 
        statistic_max_height(0) {}

    uint64_t all_gas_amount;
    uint64_t root_all_gas_amount;
    std::map<uint64_t, std::unordered_map<std::string, uint64_t>> join_elect_stoke_map;
    std::map<uint64_t, std::unordered_map<std::string, uint32_t>> join_elect_shard_map;
    std::map<uint64_t, std::unordered_map<std::string, StatisticMemberInfoItem>> height_node_collect_info_map;
    std::unordered_map<std::string, std::string> id_pk_map;
    std::unordered_map<std::string, elect::protobuf::BlsPublicKey*> id_agg_bls_pk_map;
    std::unordered_map<std::string, elect::protobuf::BlsPopProof*> id_agg_bls_pk_proof_map;
    uint64_t statistic_min_height;
    uint64_t statistic_max_height;
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

    // ZJC_DEBUG("gid: %s, pk: %s, to: %s, amount: %lu, gas limit: %lu, gas price: %lu, "
    //     "step: %d, contract code: %s, input: %s, prepayment: %lu, key: %s, value: %s", 
    //     common::Encode::HexEncode(tx_info.gid()).c_str(),
    //     common::Encode::HexEncode(tx_info.pubkey()).c_str(),
    //     common::Encode::HexEncode(tx_info.to()).c_str(),
    //     tx_info.amount(),
    //     tx_info.gas_limit(),
    //     tx_info.gas_price(),
    //     tx_info.step(),
    //     common::Encode::HexEncode(tx_info.contract_code()).c_str(),
    //     common::Encode::HexEncode(tx_info.contract_input()).c_str(),
    //     tx_info.contract_prepayment(),
    //     common::Encode::HexEncode(tx_info.key()).c_str(),
    //     common::Encode::HexEncode(tx_info.value()).c_str());

    // ZJC_DEBUG("message: %s", common::Encode::HexEncode(message).c_str());
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

static inline bool IsRootNode() {
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId || 
            common::GlobalInfo::Instance()->network_id() == 
            network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset) {
        return true;
    }

    return false;
}

static inline bool IsUserTransaction(uint32_t step) {
    if (step != pools::protobuf::kNormalFrom && 
            step != pools::protobuf::kContractCreate && 
            step != pools::protobuf::kContractExcute && 
            step != pools::protobuf::kContractGasPrepayment && 
            step != pools::protobuf::kJoinElect && 
            step != pools::protobuf::kCreateLibrary) {
        return false;
    }

    return true;   
}

static inline bool IsTxUseFromAddress(uint32_t step) {
    switch (step) {
        case pools::protobuf::kNormalTo:
        case pools::protobuf::kRootCreateAddress:
        // case pools::protobuf::kContractCreateByRootTo:
        case pools::protobuf::kConsensusLocalTos:
        case pools::protobuf::kConsensusRootElectShard:
        case pools::protobuf::kConsensusRootTimeBlock:
        case pools::protobuf::kConsensusCreateGenesisAcount:
        case pools::protobuf::kContractExcute:
        case pools::protobuf::kStatistic:
        case pools::protobuf::kContractCreate:
        case pools::protobuf::kCreateLibrary:
            return false;
        case pools::protobuf::kJoinElect:
        case pools::protobuf::kNormalFrom:
        case pools::protobuf::kContractCreateByRootFrom:
        case pools::protobuf::kContractGasPrepayment:
        case pools::protobuf::kPoolStatisticTag:
            return true;
        default:
            assert(false);
            return false;
    }
}


};  // namespace pools

};  // namespace shardora
