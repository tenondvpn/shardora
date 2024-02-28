#pragma once

#include <consensus/zbft/contract_create_by_root_from_tx_item.h>
#include <consensus/zbft/contract_create_by_root_to_tx_item.h>
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
#include "consensus/zbft/create_library.h"
#include "consensus/zbft/cross_tx_item.h"
#include "consensus/zbft/elect_tx_item.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/join_elect_tx_item.h"
#include "consensus/zbft/root_cross_tx_item.h"
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

static const uint64_t COMMIT_MSG_TIMEOUT_MS = 500; // commit msg 处理超时时间
enum class BackupBftStage {
    WAITING_PREPARE,
    PREPARE_RECEIVED,
    PRECOMMIT_RECEIVED,
    COMMIT_RECEIVED,
};

class WaitingTxsPools;
class BftManager : public Consensus {
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
    void NotifyRotationLeader(
        uint64_t elect_height,
        int32_t pool_mod_index,
        uint32_t old_leader_idx,
        uint32_t new_leader_idx);
    BftManager();
    virtual ~BftManager();
    void RotationLeader(
        int32_t leader_mod_num,
        uint64_t elect_height,
        uint32_t new_leader_idx);

private:
    int AddBft(ZbftPtr& bft_ptr);
    ZbftPtr GetBft(uint32_t pool_index, const std::string& gid);
    ZbftPtr GetBftWithHash(uint32_t pool_index, const std::string& hash);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    ZbftPtr Start(
        uint8_t thread_index,
        ZbftPtr commited_bft_ptr);
    ZbftPtr StartBft(
        const std::shared_ptr<ElectItem>& elect_item,
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        ZbftPtr commited_bft_ptr);
    void RemoveBft(uint32_t pool_index, const std::string& gid);
    void RemoveBftWithBlockHeight(uint32_t pool_index, uint64_t height);
    int LeaderPrepare(
        const ElectItem& elect_item,
        ZbftPtr& bft_ptr,
        ZbftPtr& commited_bft_ptr);
    void LeaderHandleZbftMessage(const transport::MessagePtr& msg_ptr);
    int BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCommit(
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr);
    int BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    void CheckTimeout(uint8_t thread_index);
    void CheckMessageTimeout(uint8_t thread_index);
    int LeaderHandlePrepare(const transport::MessagePtr& msg_ptr);
    int LeaderCallPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    ZbftPtr CreateBftPtr(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr,
        std::vector<uint8_t>* invalid_txs);
    void BackupHandleZbftMessage(
        uint8_t thread_index,
        const transport::MessagePtr& msg_ptr);
    int BackupPrepare(
        const ElectItem& elect_item,
        const transport::MessagePtr& msg_ptr,
        std::vector<uint8_t>* invalid_txs);
    bool IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info);
    void HandleLocalCommitBlock(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    int InitZbftPtr(int32_t leader_idx, const ElectItem& elect_item, ZbftPtr& bft_ptr);
    bool VerifyBackupIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    bool VerifyLeaderIdValid(const ElectItem& elect_item, const transport::MessagePtr& msg_ptr);
    bool CheckAggSignValid(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    bool SetBackupEcdhData(transport::MessagePtr& msg_ptr, common::BftMemberPtr& mem_ptr);
    bool LeaderSignMessage(transport::MessagePtr& msg_ptr);
    ZbftPtr LeaderGetZbft(
        const transport::MessagePtr& msg_ptr,
        const std::string& gid);
    void SyncConsensusBlock(
        uint8_t thread_idx,
        uint32_t pool_index,
        const std::string& bft_gid);
    void HandleSyncConsensusBlock(const transport::MessagePtr& msg_ptr);
    bool AddSyncKeyValue(transport::protobuf::Header* msg, const block::protobuf::Block& block);
    void SaveKeyValue(const transport::protobuf::Header& msg);
    void PopAllPoolTxs(uint8_t thread_index);
    void LeaderBroadcastBlock(
        uint8_t thread_index,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastLocalTosBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastWaitingBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastBlock(
        uint8_t thread_idx,
        uint32_t des_shard,
        const std::shared_ptr<block::protobuf::Block>& block);
    void RegisterCreateTxCallbacks();
    void SetThreadItem(
        uint32_t leader_count,
        int32_t local_node_pool_mod_num,
        std::shared_ptr<PoolTxIndexItem>* thread_set);
    void ReConsensusBft(ZbftPtr& zbft_ptr);
    int ChangePrecommitBftLeader(
        ZbftPtr& bft_ptr,
        uint32_t leader_idx,
        const ElectItem& elect_item);
    void AddWaitingBlock(std::shared_ptr<block::protobuf::Block>& block_ptr);
    void RemoveWaitingBlock(uint32_t pool_index, uint64_t height);
    void ReConsensusPrepareBft(const ElectItem& elect_item, ZbftPtr& bft_ptr);
    void HandleSyncedBlock(uint8_t thread_idx, std::shared_ptr<block::protobuf::Block>& block_ptr);
    void ReConsensusChangedLeaderBft(ZbftPtr& bft_ptr);
    bool CheckChangedLeaderBftsValid(uint32_t pool, uint64_t height, const std::string& gid);
    void BroadcastInvalidGids(uint8_t thread_idx);
    void CheckInvalidGids(uint8_t thread_idx);
    void LeaderRemoveTimeoutPrepareBft(ZbftPtr& bft_ptr);
    void BackupSendPrepareMessage(
        const ElectItem& elect_item,
        const transport::MessagePtr& leader_msg_ptr,
        bool agree,
        const std::vector<uint8_t>& invalid_txs);
    void BackupSendPrecommitMessage(
        const ElectItem& elect_item,
        const transport::MessagePtr& leader_msg_ptr,
        bool agree);
    void LeaderSendPrecommitMessage(const transport::MessagePtr& leader_msg_ptr, bool agree);
    void LeaderSendCommitMessage(const transport::MessagePtr& leader_msg_ptr, bool agree);
    void HandleCommitedSyncBlock(uint8_t thread_idx, const zbft::protobuf::ZbftMessage& req_bft_msg);
    std::shared_ptr<WaitingTxsItem> get_txs_ptr(
        std::shared_ptr<PoolTxIndexItem>& thread_item,
        ZbftPtr& commited_bft_ptr);

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

    pools::TxItemPtr CreateLibraryTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<CreateLibrary>(msg_ptr, account_mgr_, security_ptr_);
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

    pools::TxItemPtr CreateCrossTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<CrossTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateRootCrossTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<RootCrossTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateContractUserCreateCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCreateCall>(
            contract_mgr_, db_, msg_ptr, account_mgr_, security_ptr_);
    }

	pools::TxItemPtr CreateContractByRootFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCreateByRootFromTxItem>(
            contract_mgr_, db_, msg_ptr, account_mgr_, security_ptr_);
    }

	pools::TxItemPtr CreateContractByRootToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCreateByRootToTxItem>(
            contract_mgr_, db_, msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateContractUserCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCall>(db_, msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateContractCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCall>(
            contract_mgr_, gas_prepayment_, db_, msg_ptr, account_mgr_, security_ptr_);
    }

    static const uint32_t kCheckTimeoutPeriodMilli = 1000lu;

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
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
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
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
    block::BlockAggValidCallback block_agg_valid_func_ = nullptr;
    ZbftPtr pools_with_zbfts_[common::kInvalidPoolIndex] = { nullptr };
    std::deque<transport::MessagePtr> backup_prapare_msg_queue_[common::kMaxThreadCount];
    std::map<uint64_t, std::shared_ptr<block::protobuf::Block>> waiting_blocks_[common::kInvalidPoolIndex];
    std::map<uint64_t, std::shared_ptr<block::protobuf::Block>, std::greater<uint64_t>> waiting_agg_verify_blocks_[common::kInvalidPoolIndex];
    ZbftPtr changed_leader_pools_height_[common::kInvalidPoolIndex] = { nullptr };
    common::LimitHashMap<std::string, ZbftPtr> removed_preapare_gid_with_hash_[common::kInvalidPoolIndex];
    uint64_t prev_broadcast_invalid_gid_tm_[common::kMaxThreadCount] = { 0 };
    std::unordered_set<std::string> broadcasted_gids_[common::kMaxThreadCount];
    std::shared_ptr<BftMessageInfo> gid_with_msg_map_[common::kInvalidPoolIndex];
    uint64_t pools_prev_bft_timeout_[common::kInvalidPoolIndex] = { 0 };

    inline BackupBftStage GetBackupBftStage(std::shared_ptr<BftMessageInfo> bft_msgs) {
        if (bft_msgs == nullptr || bft_msgs->msgs[0] == nullptr) {
            return BackupBftStage::WAITING_PREPARE;
        }

        if (bft_msgs->msgs[2] != nullptr) {
            return BackupBftStage::COMMIT_RECEIVED;
        }
        if (bft_msgs->msgs[1] != nullptr) {
            return BackupBftStage::PRECOMMIT_RECEIVED;
        }
        return BackupBftStage::PREPARE_RECEIVED;
    }

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
