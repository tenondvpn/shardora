#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "elect/elect_manager.h"
#include "security/security.h"
#include "vss/vss_manager.h"

namespace zjchain {

namespace consensus {

struct ElectNodeInfo {
    uint64_t fts_value;
    uint64_t stoke;
    uint64_t stoke_diff;
    uint64_t tx_count;
    int32_t credit;
    int32_t area_weight;
    std::string id;
    uint32_t index;
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
        std::shared_ptr<vss::VssManager>& vss_mgr)
        : TxItemBase(msg, account_mgr, sec_ptr),
        prefix_db_(prefix_db),
        elect_mgr_(elect_mgr),
        vss_mgr_(vss_mgr) {}
    virtual ~ElectTxItem() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
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
        uint32_t count,
        const pools::protobuf::ElectStatistic& elect_statistic,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr>& elect_nodes);
    void FtsGetNodes(
        std::vector<NodeDetailPtr>& elect_nodes,
        bool weed_out,
        uint32_t count,
        std::set<uint32_t>& tmp_res_nodes);
    void SmoothFtsValue(
        std::vector<NodeDetailPtr>& elect_nodes,
        uint64_t* max_fts_val);
    void CreateNewElect(
        const std::vector<NodeDetailPtr>& elect_nodes,
        const std::vector<NodeDetailPtr>& new_elect_nodes,
        block::protobuf::BlockTx& block_tx);

    static const uint32_t kFtsWeedoutDividRate = 10u;
    static const uint32_t kFtsNewElectJoinRate = 5u;

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::vector<std::shared_ptr<ElectNodeInfo>> elect_nodes_;
    uint64_t max_stoke_ = 0;
    uint64_t min_stoke_ = common::kInvalidUint64;
    uint32_t max_area_weight_ = 0;
    uint32_t min_area_weight_ = common::kInvalidUint32;
    uint32_t max_tx_count_ = 0;
    uint32_t min_tx_count_ = common::kInvalidUint32;
    int32_t max_credit_ = 0;
    int32_t min_credit_ = common::kInvalidInt32;

    DISALLOW_COPY_AND_ASSIGN(ElectTxItem);
};

};  // namespace consensus

};  // namespace zjchain
