#pragma once
#include "consensus/consensus.h"
#include "elect_info.h"
#include "view_block_chain_manager.h"
#include "crypto.h"
#include "pacemaker.h"


// #include "tx/contract_gas_prepayment.h"
// #include "tx/from_tx_item.h"
// #include "tx/to_tx_item.h"
// #include "tx/statistic_tx_item.h"
// #include "tx/to_tx_local_item.h"
// #include "tx/time_block_tx.h"
// #include "tx/root_to_tx_item.h"
// #include "tx/create_library.h"
// #include "tx/elect_tx_item.h"
// #include "tx/cross_tx_item.h"
// #include "tx/root_cross_tx_item.h"
// #include "tx/contract_user_call.h"
// #include "tx/join_elect_tx_item.h"
// #include "tx/contract_user_create_call.h"
// #include "tx/contract_create_by_root_from_tx_item.h"
// #include "tx/contract_create_by_root_to_tx_item.h"
// #include "tx/contract_call.h"

#include <unordered_map>
#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/tick.h"
#include "common/limit_hash_map.h"
#include "consensus/zbft/contract_gas_prepayment.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "pools/tx_pool_manager.h"
#include "protos/elect.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "timeblock/time_block_manager.h"
#include "transport/transport_utils.h"



namespace shardora {

namespace vss {
    class VssManager;
}

namespace contract {
    class ContractManager;
};

namespace consensus {
using namespace shardora::hotstuff;

class WaitingTxsPools;
class HotstuffManager : public Consensus {
public:
    int Init(
        block::BlockAggValidCallback block_agg_valid_func,
        std::shared_ptr<contract::ContractManager>& contract_mgr,
        std::shared_ptr<consensus::ContractGasPrepayment>& gas_prepayment,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count,
        BlockCacheCallback new_block_cache_callback);
    void OnNewElectBlock(
        uint64_t block_tm_ms,
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& sec_key);
    HotstuffManager();
    virtual ~HotstuffManager();
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    Status VerifyViewBlockItem(const view_block::protobuf::ViewBlockItem& pb_view_block, 
        const uint32_t& pool_index, const uint32_t& elect_height);
    void DoCommitBlock(const view_block::protobuf::ViewBlockItem& pb_view_block, const uint32_t& pool_index);
private:

    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void RegisterCreateTxCallbacks();

    void DoProposeMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index);
    void DoVoteMsg(const hotstuff::protobuf::ProposeMsg& pro_msg, const uint32_t& pool_index);
    Status SendTranMsg(hotstuff::protobuf::HotstuffMessage& hotstuff_msg);

    Status VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index, 
        std::shared_ptr<ViewBlock>& view_block);
    
    std::unordered_map<uint32_t, std::shared_ptr<Pacemaker>> pool_Pacemaker_;
    std::shared_ptr<ViewBlockChainManager> v_block_mgr_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<Crypto> crypto_;
    
    // pools::TxItemPtr CreateFromTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<FromTxItem>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateToTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ToTxItem>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateStatisticTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<StatisticTxItem>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateToTxLocal(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ToTxLocalItem>(
    //         msg_ptr->header.tx_proto(), db_, gas_prepayment_, 
    //         account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateTimeblockTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<TimeBlockTx>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateRootToTxItem(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<RootToTxItem>(
    //         max_consensus_sharding_id_,
    //         msg_ptr->header.tx_proto(),
    //         vss_mgr_,
    //         account_mgr_,
    //         security_ptr_,
    //         msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateLibraryTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<CreateLibrary>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateElectTx(const transport::MessagePtr& msg_ptr) {
    //     if (first_timeblock_timestamp_ == 0) {
    //         uint64_t height = 0;
    //         prefix_db_->GetGenesisTimeblock(&height, &first_timeblock_timestamp_);
    //     }

    //     return std::make_shared<ElectTxItem>(
    //         msg_ptr->header.tx_proto(),
    //         account_mgr_,
    //         security_ptr_,
    //         prefix_db_,
    //         elect_mgr_,
    //         vss_mgr_,
    //         bls_mgr_,
    //         first_timeblock_timestamp_,
    //         false,
    //         max_consensus_sharding_id_ - 1,
    //         msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateJoinElectTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<JoinElectTxItem>(
    //         msg_ptr->header.tx_proto(), 
    //         account_mgr_, 
    //         security_ptr_, 
    //         prefix_db_, 
    //         elect_mgr_, 
    //         msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateCrossTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<CrossTxItem>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateRootCrossTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<RootCrossTxItem>(
    //         msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateContractUserCreateCallTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ContractUserCreateCall>(
    //         contract_mgr_, 
    //         db_, 
    //         msg_ptr->header.tx_proto(), 
    //         account_mgr_, 
    //         security_ptr_, 
    //         msg_ptr->address_info);
    // }

	// pools::TxItemPtr CreateContractByRootFromTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ContractCreateByRootFromTxItem>(
    //         contract_mgr_, 
    //         db_, 
    //         msg_ptr->header.tx_proto(), 
    //         account_mgr_, 
    //         security_ptr_, 
    //         msg_ptr->address_info);
    // }

	// pools::TxItemPtr CreateContractByRootToTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ContractCreateByRootToTxItem>(
    //         contract_mgr_, 
    //         db_, 
    //         msg_ptr->header.tx_proto(), 
    //         account_mgr_, 
    //         security_ptr_, 
    //         msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateContractUserCallTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ContractUserCall>(
    //         db_, msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    // }

    // pools::TxItemPtr CreateContractCallTx(const transport::MessagePtr& msg_ptr) {
    //     return std::make_shared<ContractCall>(
    //         contract_mgr_, 
    //         gas_prepayment_, 
    //         db_, 
    //         msg_ptr->header.tx_proto(), 
    //         account_mgr_, 
    //         security_ptr_, 
    //         msg_ptr->address_info);
    // }

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;

    std::atomic<uint32_t> tps_{ 0 };
    std::atomic<uint32_t> pre_tps_{ 0 };
    uint64_t tps_btime_{ 0 };
    common::Tick timeout_tick_;
    common::Tick block_to_db_tick_;
    common::Tick verify_block_tick_;
    common::Tick leader_resend_tick_;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint8_t thread_count_ = 0;
    std::shared_ptr<WaitingTxsPools> txs_pools_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;

    uint64_t bft_gids_index_[common::kMaxThreadCount];
    uint32_t prev_checktime_out_milli_ = 0;
    uint32_t minimal_node_count_to_consensus_ = common::kInvalidUint32;
    BlockCacheCallback new_block_cache_callback_ = nullptr;
    // std::shared_ptr<ElectItem> elect_items_[2] = { nullptr };
    uint32_t elect_item_idx_ = 0;
    uint64_t prev_tps_tm_us_ = 0;
    uint32_t prev_count_ = 0;
    common::SpinMutex prev_count_mutex_;
    uint64_t prev_test_bft_size_[common::kMaxThreadCount] = { 0 };
    uint32_t max_consensus_sharding_id_ = 3;
    uint64_t first_timeblock_timestamp_ = 0;
    block::BlockAggValidCallback block_agg_valid_func_ = nullptr;
    std::deque<transport::MessagePtr> backup_prapare_msg_queue_[common::kMaxThreadCount];
    std::map<uint64_t, std::shared_ptr<block::protobuf::Block>> waiting_blocks_[common::kInvalidPoolIndex];
    std::map<uint64_t, std::shared_ptr<block::protobuf::Block>, std::greater<uint64_t>> waiting_agg_verify_blocks_[common::kInvalidPoolIndex];
    uint64_t pools_prev_bft_timeout_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pools_send_to_leader_tm_ms_[common::kInvalidPoolIndex] = { 0 };

    DISALLOW_COPY_AND_ASSIGN(HotstuffManager);
};

}  // namespace consensus

}  // namespace shardora
