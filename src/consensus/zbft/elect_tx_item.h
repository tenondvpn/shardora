#pragma once

#include "block/account_manager.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "consensus/zbft/tx_item_base.h"
#include "elect/elect_manager.h"
#include "security/security.h"
#include "vss/vss_manager.h"

namespace shardora {

namespace consensus {

struct ElectNodeInfo {
    ElectNodeInfo() : leader_mod_index(-1), mining_token(0) {}
    uint64_t fts_value;
    uint64_t stoke;
    uint64_t stoke_diff;
    uint64_t tx_count;
    uint64_t gas_sum;
    int32_t credit;
    int32_t area_weight;
    std::string pubkey;
    uint32_t index;
    int32_t leader_mod_index;
    uint64_t mining_token;
    uint64_t consensus_gap;
    libff::alt_bn128_G2 agg_bls_pk; // agg bls 公钥
    libff::alt_bn128_G1 agg_bls_pk_proof; // agg bls pk 的 pop proof
};

typedef std::shared_ptr<ElectNodeInfo> NodeDetailPtr;

class ElectTxItem : public TxItemBase {
public:
    ElectTxItem(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        std::shared_ptr<protos::PrefixDb>& prefix_db,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        uint64_t first_timeblock_timestamp,
        bool stop_mining,
        uint32_t network_count,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info),
        prefix_db_(prefix_db),
        elect_mgr_(elect_mgr),
        vss_mgr_(vss_mgr),
        bls_mgr_(bls_mgr),
        first_timeblock_timestamp_(first_timeblock_timestamp),
        stop_mining_(stop_mining),
        network_count_(network_count) {}
    virtual ~ElectTxItem() {}
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx);
    virtual int HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost &zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx &block_tx);

private:
    int processElect(
        zjcvm::ZjchainHost& zjc_host,
        view_block::protobuf::ViewBlockItem& view_block,
        shardora::block::protobuf::BlockTx &block_tx);

    int getMaxElectHeightInfo(
        const shardora::pools::protobuf::PoolStatisticItem *&statistic, 
        shardora::common::MembersPtr &members);

    void JoinNewNodes2ElectNodes(
        shardora::common::MembersPtr &members,
        std::vector<shardora::consensus::NodeDetailPtr> &elect_nodes,
        uint32_t min_area_weight,
        uint32_t min_tx_count);

    int CheckWeedout(
        common::MembersPtr &members,
        const pools::protobuf::PoolStatisticItem &statistic_item,
        uint32_t *min_area_weight,
        uint32_t *min_tx_count,
        std::vector<NodeDetailPtr> &elect_nodes);
    int GetJoinElectNodesCredit(
        uint32_t index,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr> &elect_nodes_to_choose,
        std::vector<NodeDetailPtr> &elect_nodes);
    void FtsGetNodes(
        std::vector<NodeDetailPtr> &elect_nodes,
        bool weed_out,
        uint32_t count,
        std::set<uint32_t> &tmp_res_nodes);
    void SmoothFtsValue(
        std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t *max_fts_val);
    int CreateNewElect(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::Block &block,
        const std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t gas_for_root,
        block::protobuf::BlockTx &block_tx);
    void MiningToken(
        uint32_t statistic_sharding_id,
        std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t all_gas_amount,
        uint64_t *gas_for_root);
    uint64_t GetMiningMaxCount(uint64_t max_tx_count);
    void GetIndexNodes(
        uint32_t index,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr> *elect_nodes_to_choose);
    void ChooseNodeForEachIndex(
        bool hold_pos,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr> &elect_nodes);
    void SetPrevElectInfo(
        const elect::protobuf::ElectBlock &elect_block,
        block::protobuf::Block &block_item);

    static const uint32_t kFtsWeedoutDividRate = 10u;
    static const uint32_t kFtsNewElectJoinRate = 5u;
    static const uint32_t kFtsMinDoubleNodeCount = 256u;

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
    std::shared_ptr<hotstuff::ViewBlockChain> view_block_chain_ = nullptr;
    std::string unique_hash_;
    pools::protobuf::ElectStatistic elect_statistic_;

    DISALLOW_COPY_AND_ASSIGN(ElectTxItem);
};

};  // namespace consensus

};  // namespace shardora
