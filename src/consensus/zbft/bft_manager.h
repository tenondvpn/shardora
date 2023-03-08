#pragma once

#include <unordered_map>

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/tick.h"
#include "common/limit_hash_map.h"
#include "consensus/consensus.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/to_tx_item.h"
#include "consensus/zbft/to_tx_local_item.h"
#include "consensus/zbft/zbft.h"
#include "db/db.h"
#include "elect/member_manager.h"
#include "elect/elect_manager.h"
#include "pools/tx_pool_manager.h"
#include "protos/elect.pb.h"
#include "protos/hotstuff.pb.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace consensus {

class WaitingTxsPools;
class BftManager : public Consensus {
public:
    int Init(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb,
        uint8_t thread_count);
    void OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members);
    BftManager();
    virtual ~BftManager();
    int AddBft(ZbftPtr& bft_ptr);
    ZbftPtr GetBft(uint8_t thread_index, const std::string& gid, bool leader);
    uint32_t GetMemberIndex(uint32_t network_id, const std::string& node_id);

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    ZbftPtr Start(uint8_t thread_index, const transport::MessagePtr& prepare_msg_ptr);
    ZbftPtr StartBft(
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        const transport::MessagePtr& prepare_msg_ptr);
    void RemoveBft(uint8_t thread_idx, const std::string& gid, bool is_leader);
    int LeaderPrepare(ZbftPtr& bft_ptr, const transport::MessagePtr& prepare_msg_ptr);
    int BackupPrepare(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int BackupPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int BackupCommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    void CheckTimeout(uint8_t thread_index);
    int LeaderCallPrecommit(ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCallCommit(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    ZbftPtr CreateBftPtr(const transport::MessagePtr& msg_ptr);
    void HandleHotstuffMessage(
        ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr);
    int LeaderCallPrecommitOppose(const ZbftPtr& bft_ptr, const transport::MessagePtr& msg_ptr);
    int LeaderCallCommitOppose(const transport::MessagePtr& msg_ptr, ZbftPtr& bft_ptr);
    void BackupSendOppose(
        const transport::MessagePtr& msg_ptr,
        ZbftPtr& bft_ptr);
    void LeaderHandleBftOppose(
        const ZbftPtr& bft_ptr,
        const transport::MessagePtr& msg_ptr);
    ZbftPtr BackupHandleHotstuffMessage(
        uint8_t thread_index,
        const transport::MessagePtr& msg_ptr);
    bool IsCreateContractLibraray(const block::protobuf::BlockTx& tx_info);
    void HandleLocalCommitBlock(int32_t thread_idx, ZbftPtr& bft_ptr);
    int InitZbftPtr(bool leader, ZbftPtr& bft_ptr);
    bool VerifyBackupIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    bool VerifyLeaderIdValid(
        const transport::MessagePtr& msg_ptr,
        common::BftMemberPtr& mem_ptr);
    void CreateResponseMessage(
        bool response_to_leader,
        const std::vector<ZbftPtr>& zbft_vec,
        const transport::MessagePtr& msg_ptr);
    int CheckPrecommit(const transport::MessagePtr& msg_ptr);
    int CheckCommit(const transport::MessagePtr& msg_ptr);
    pools::TxItemPtr CreateFromTx(transport::MessagePtr& msg_ptr) {
        return std::make_shared<FromTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateToTx(transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxItem>(msg_ptr, account_mgr_, security_ptr_);
    }

    pools::TxItemPtr CreateToTxLocal(transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxLocalItem>(msg_ptr, db_, account_mgr_, security_ptr_);
    }

    static const uint32_t kCheckTimeoutPeriodMilli = 3000lu;

    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::unordered_map<std::string, ZbftPtr>* bft_hash_map_ = nullptr;
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
    uint8_t thread_count_ = 0;
    std::shared_ptr<PoolTxIndexItem> thread_set_[common::kMaxThreadCount];
    std::shared_ptr<WaitingTxsPools> txs_pools_ = nullptr;
    std::string bft_gids_[common::kMaxThreadCount];
    uint64_t bft_gids_index_[common::kMaxThreadCount];
    uint32_t prev_checktime_out_milli_ = 0;


#ifdef ZJC_UNITTEST
    void ResetTest() {
        now_msg_ = nullptr;
    }

    // just for test
    transport::MessagePtr now_msg_ = nullptr;
    bool test_for_prepare_evil_ = false;
    bool test_for_precommit_evil_ = false;
#endif

    DISALLOW_COPY_AND_ASSIGN(BftManager);
};

}  // namespace consensus

}  // namespace zjchain
