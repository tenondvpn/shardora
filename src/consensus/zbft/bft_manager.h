#pragma once

#include <unordered_map>

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/tick.h"
#include "common/limit_hash_map.h"
#include "consensus/consensus.h"
#include "consensus/zbft/contract_call.h"
#include "consensus/zbft/contract_gas_prepayment.h"
#include "consensus/zbft/contract_user_call.h"
#include "consensus/zbft/contract_user_create_call.h"
#include "consensus/zbft/elect_tx_item.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/join_elect_tx_item.h"
#include "consensus/zbft/root_to_tx_item.h"
#include "consensus/zbft/statistic_tx_item.h"
#include "consensus/zbft/time_block_tx.h"
#include "consensus/zbft/to_tx_item.h"
#include "consensus/zbft/to_tx_local_item.h"
#include "consensus/zbft/zbft.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "pools/tx_pool_manager.h"
#include "protos/elect.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "protos/zbft.pb.h"
#include "security/security.h"
#include "timeblock/time_block_manager.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace vss {
    class VssManager;
}

namespace contract {
    class ContractManager;
};

namespace consensus {

class WaitingTxsPools;
class BftManager : public Consensus {
public:
    int Init(
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
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count,
        BlockCacheCallback new_block_cache_callback);
    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, common::MembersPtr& members);
    void NotifyRotationLeader(
        uint64_t elect_height,
        uint32_t pool_mod_index,
        uint32_t old_leader_idx,
        uint32_t new_leader_idx);
    BftManager();
    virtual ~BftManager();
    int AddBft(ZbftPtr& bft_ptr);
    ZbftPtr GetBft(uint8_t thread_index, const std::string& gid, bool leader);

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    ZbftPtr Start(
        uint8_t thread_index,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr);
    ZbftPtr StartBft(
        const ElectItem& elect_item,
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr);
    void RemoveBft(uint8_t thread_idx, const std::string& gid, bool is_leader);
    int LeaderPrepare(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& prepare_msg_ptr);
    int LeaderHandleZbftMessage(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr);
    int BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCommit(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr);
    int BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    void CheckTimeout(uint8_t thread_index);
    int LeaderCallPrecommit(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr);
    int LeaderCallCommit(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr);
    ZbftPtr CreateBftPtr(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr);
    void BackupHandleZbftMessage(
        uint8_t thread_index,
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr);
    void BackupPrepare(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr);
    bool IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info);
    void HandleLocalCommitBlock(int32_t thread_idx, ZbftPtr& bft_ptr);
    int InitZbftPtr(int32_t leader_idx, const ElectItem& elect_item, ZbftPtr& bft_ptr);
    bool VerifyBackupIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    bool VerifyLeaderIdValid(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr);
    void CreateResponseMessage(
        const ElectItem& elect_item,
        bool response_to_leader,
        const std::vector<ZbftPtr>& zbft_vec,
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    int CheckPrecommit(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr);
    int CheckCommit(const transport::MessagePtr& msg_ptr, bool backup_agree_commit);
    bool CheckAggSignValid(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    void SetDefaultResponse(const transport::MessagePtr& msg_ptr);
    bool SetBackupEcdhData(transport::MessagePtr& msg_ptr, common::BftMemberPtr& mem_ptr);
    bool LeaderSignMessage(transport::MessagePtr& msg_ptr);
    void ClearBft(const transport::MessagePtr& msg_ptr);
    ZbftPtr LeaderGetZbft(const transport::MessagePtr& msg_ptr, const std::string& gid);
    void SyncConsensusBlock(
        const ElectItem& elect_item,
        uint8_t thread_idx,
        uint32_t pool_index,
        const std::string& bft_gid);
    void HandleSyncConsensusBlock(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr);
    bool AddSyncKeyValue(transport::protobuf::Header* msg, const block::protobuf::Block& block);
    void SaveKeyValue(const transport::protobuf::Header& msg);
    void PopAllPoolTxs(uint8_t thread_index);
    void LeaderBroadcastBlock(
        uint8_t thread_index,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastLocalTosBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastStatisticBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastWaitingBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastElectBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastTimeblock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void RegisterCreateTxCallbacks();

    pools::TxItemPtr CreateFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<FromTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateStatisticTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<StatisticTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateToTxLocal(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxLocalItem>(
            msg_ptr, db_, gas_prepayment_, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateTimeblockTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<TimeBlockTx>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateRootToTxItem(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<RootToTxItem>(
            max_consensus_sharding_id_,
            msg_ptr,
            vss_mgr_,
            account_mgr_,
            security_ptr_);
    }

    pools::TxItemPtr CreateElectTx(const transport::MessagePtr& msg_ptr) {
        if (first_timeblock_timestamp_ == 0) {
            uint64_t height = 0;
            prefix_db_->GetGenesisTimeblock(&height, &first_timeblock_timestamp_);
        }

        return std::make_shared<ElectTxItem>(
            msg_ptr,
            account_mgr_,
            security_ptr_,
            prefix_db_,
            elect_mgr_,
            vss_mgr_,
            bls_mgr_,
            first_timeblock_timestamp_,
            false,
            max_consensus_sharding_id_ - 1);
    }

    pools::TxItemPtr CreateJoinElectTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<JoinElectTxItem>(
            msg_ptr, account_mgr_, security_ptr_, prefix_db_, elect_mgr_);
    }

    pools::TxItemPtr CreateContractUserCreateCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCreateCall>(
            contract_mgr_, db_, msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateContractUserCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCall>(db_, msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateContractCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCall>(
            contract_mgr_, gas_prepayment_, db_, msg_ptr, account_mgr_, security_ptr_);
    }

    static const uint32_t kCheckTimeoutPeriodMilli = 3000lu;

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::unordered_map<std::string, ZbftPtr>* bft_hash_map_ = nullptr;
    std::queue<ZbftPtr>* bft_queue_ = nullptr;
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
    uint8_t thread_count_ = 0;
    std::shared_ptr<WaitingTxsPools> txs_pools_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::string bft_gids_[common::kMaxThreadCount];
    uint64_t bft_gids_index_[common::kMaxThreadCount];
    uint32_t prev_checktime_out_milli_ = 0;
    uint32_t minimal_node_count_to_consensus_ = common::kInvalidUint32;
    BlockCacheCallback new_block_cache_callback_ = nullptr;
    std::shared_ptr<ElectItem> elect_items_[2] = { nullptr };
    uint32_t elect_item_idx_ = 0;
    uint64_t prev_tps_tm_us_ = 0;
    uint32_t prev_count_ = 0;
    common::SpinMutex prev_count_mutex_;
    uint64_t prev_test_bft_size_[common::kMaxThreadCount] = { 0 };
    uint32_t max_consensus_sharding_id_ = 3;
    uint64_t first_timeblock_timestamp_ = 0;

#ifdef ZJC_UNITTEST
    void ResetTest() {
        now_msg_ = nullptr;
    }

    // just for test
    transport::MessagePtr* now_msg_ = nullptr;
    bool test_for_prepare_evil_ = false;
    bool test_for_precommit_evil_ = false;
#endif

    DISALLOW_COPY_AND_ASSIGN(BftManager);
};

}  // namespace consensus

}  // namespace zjchain
