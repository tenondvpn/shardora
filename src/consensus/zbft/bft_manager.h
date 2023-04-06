#pragma once

#include <unordered_map>

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/tick.h"
#include "common/limit_hash_map.h"
#include "consensus/consensus.h"
#include "consensus/zbft/contract_user_create_call.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/root_to_tx_item.h"
#include "consensus/zbft/time_block_tx.h"
#include "consensus/zbft/to_tx_item.h"
#include "consensus/zbft/to_tx_local_item.h"
#include "consensus/zbft/zbft.h"
#include "db/db.h"
#include "elect/member_manager.h"
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

namespace consensus {

class WaitingTxsPools;
class BftManager : public Consensus {
public:
    int Init(
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count,
        BlockCacheCallback new_block_cache_callback);
    void OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members);
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
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        ZbftPtr& prev_bft,
        const transport::MessagePtr& prepare_msg_ptr);
    void RemoveBft(uint8_t thread_idx, const std::string& gid, bool is_leader);
    int LeaderPrepare(ZbftPtr& bft_ptr, const transport::MessagePtr& prepare_msg_ptr);
    int LeaderHandleZbftMessage(const transport::MessagePtr& msg_ptr);
    int BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    void CheckTimeout(uint8_t thread_index);
    int LeaderCallPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCallCommit(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    ZbftPtr CreateBftPtr(const transport::MessagePtr& msg_ptr);
    int LeaderCallCommitOppose(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    void BackupHandleZbftMessage(
        uint8_t thread_index,
        const transport::MessagePtr& msg_ptr);
    void BackupPrepare(const transport::MessagePtr& msg_ptr);
    bool IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info);
    void HandleLocalCommitBlock(int32_t thread_idx, ZbftPtr& bft_ptr);
    int InitZbftPtr(bool leader, ZbftPtr& bft_ptr);
    bool VerifyBackupIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    bool VerifyLeaderIdValid(const transport::MessagePtr& msg_ptr);
    void CreateResponseMessage(
        bool response_to_leader,
        const std::vector<ZbftPtr>& zbft_vec,
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    int CheckPrecommit(const transport::MessagePtr& msg_ptr);
    int CheckCommit(const transport::MessagePtr& msg_ptr, bool backup_agree_commit);
    bool CheckAggSignValid(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    void SetDefaultResponse(const transport::MessagePtr& msg_ptr);
    bool SetBackupEcdhData(transport::MessagePtr& msg_ptr, common::BftMemberPtr& mem_ptr);
    bool LeaderSignMessage(transport::MessagePtr& msg_ptr);
    void ClearBft(const transport::MessagePtr& msg_ptr);
    ZbftPtr LeaderGetZbft(const transport::MessagePtr& msg_ptr, const std::string& gid);
    void SyncConsensusBlock(uint8_t thread_idx, uint32_t pool_index, const std::string& bft_gid);
    void HandleSyncConsensusBlock(const transport::MessagePtr& msg_ptr);
    bool AddSyncKeyValue(transport::protobuf::Header* msg, const block::protobuf::Block& block);
    void SaveKeyValue(const transport::protobuf::Header& msg);
    void PopAllPoolTxs(uint8_t thread_index);
    void LeaderBroadcastBlock(
        uint8_t thread_index,
        const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastLocalTosBlock(uint8_t thread_idx, const std::shared_ptr<block::protobuf::Block>& block);
    void RegisterCreateTxCallbacks();

    pools::TxItemPtr CreateFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<FromTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateToTxLocal(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxLocalItem>(msg_ptr, db_, account_mgr_, security_ptr_);
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

    pools::TxItemPtr CreateContractUserCreateCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCreateCall>(msg_ptr, account_mgr_, security_ptr_);
    }

    static const uint32_t kCheckTimeoutPeriodMilli = 3000lu;

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
    ElectItem elect_items_[2];
    uint32_t elect_item_idx_ = 0;
    uint64_t prev_tps_tm_us_ = 0;
    uint32_t prev_count_ = 0;
    common::SpinMutex prev_count_mutex_;
    uint64_t prev_test_bft_size_[common::kMaxThreadCount] = { 0 };
    uint32_t max_consensus_sharding_id_ = 3;

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
