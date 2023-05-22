#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "elect/elect_manager.h"
#include "security/security.h"
#include "vss/vss_manager.h"

namespace zjchain {

namespace consensus {

struct ElectNodeInfo {
    ElectNodeInfo() : leader_mod_index(-1), mining_token(0) {}
    uint64_t fts_value;
    uint64_t stoke;
    uint64_t stoke_diff;
    uint64_t tx_count;
    int32_t credit;
    int32_t area_weight;
    std::string pubkey;
    uint32_t index;
    int32_t leader_mod_index;
    uint64_t mining_token;
};

typedef std::shared_ptr<ElectNodeInfo> NodeDetailPtr;

class ElectTxItem : public TxItemBase {
public:
    ElectTxItem(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        std::shared_ptr<protos::PrefixDb>& prefix_db,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        uint64_t first_timeblock_timestamp,
        bool stop_mining,
        uint32_t network_count)
        : TxItemBase(msg, account_mgr, sec_ptr),
        prefix_db_(prefix_db),
        elect_mgr_(elect_mgr),
        vss_mgr_(vss_mgr),
        bls_mgr_(bls_mgr),
        first_timeblock_timestamp_(first_timeblock_timestamp),
        stop_mining_(stop_mining),
        network_count_(network_count) {}
    virtual ~ElectTxItem() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    int CheckWeedout(
        uint8_t thread_idx,
        common::MembersPtr& members,
        const pools::protobuf::PoolStatisticItem& statistic_item,
        uint32_t* min_area_weight,
        uint32_t* min_tx_count,
        std::vector<NodeDetailPtr>& elect_nodes);
    int GetJoinElectNodesCredit(
        uint32_t index,
        const pools::protobuf::ElectStatistic& elect_statistic,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        const std::vector<NodeDetailPtr>& elect_nodes_to_choose,
        std::vector<NodeDetailPtr>& elect_nodes);
    void FtsGetNodes(
        std::vector<NodeDetailPtr>& elect_nodes,
        bool weed_out,
        uint32_t count,
        std::set<uint32_t>& tmp_res_nodes);
    void SmoothFtsValue(
        std::vector<NodeDetailPtr>& elect_nodes,
        uint64_t* max_fts_val);
    int CreateNewElect(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const std::vector<NodeDetailPtr>& elect_nodes,
        const pools::protobuf::ElectStatistic& elect_statistic,
        uint64_t gas_for_root,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        block::protobuf::BlockTx& block_tx);
    void MiningToken(
        uint8_t thread_idx,
        uint32_t statistic_sharding_id,
        std::vector<NodeDetailPtr>& elect_nodes,
        uint64_t all_gas_amount,
        uint64_t* gas_for_root);
    uint64_t GetMiningMaxCount(uint64_t max_tx_count);
    void GetIndexNodes(
        uint32_t index,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        const pools::protobuf::ElectStatistic& elect_statistic,
        std::vector<NodeDetailPtr>* elect_nodes_to_choose);
    void ChooseNodeForEachIndex(
        bool hold_pos,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        const pools::protobuf::ElectStatistic& elect_statistic,
        std::vector<NodeDetailPtr>& elect_nodes);

    static const uint32_t kFtsWeedoutDividRate = 10u;
    static const uint32_t kFtsNewElectJoinRate = 5u;

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::vector<std::shared_ptr<ElectNodeInfo>> elect_nodes_;
    uint64_t max_stoke_ = 0;
    uint64_t min_stoke_ = common::kInvalidUint64;
    uint32_t max_area_weight_ = 0;
    uint32_t min_area_weight_ = common::kInvalidUint32;
    uint32_t max_tx_count_ = 0;
    uint32_t min_tx_count_ = common::kInvalidUint32;
    int32_t max_credit_ = 0;
    int32_t min_credit_ = common::kInvalidInt32;
    std::shared_ptr<std::mt19937_64> g2_ = nullptr;
    uint64_t first_timeblock_timestamp_ = 0;
    bool stop_mining_ = false;
    uint32_t network_count_ = 2;
    std::set<std::string> added_nodes_;
    common::MembersPtr elect_members_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ElectTxItem);
};

};  // namespace consensus

};  // namespace zjchain
