#pragma once
#include <unordered_map>

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/agg_bls.h"
#include "bls/bls_manager.h"
#include "consensus/consensus.h"
#include "consensus/hotstuff/elect_info.h"
#ifdef USE_AGG_BLS
#include "consensus/hotstuff/agg_crypto.h"
#else
#include "consensus/hotstuff/crypto.h"
#endif
#include "consensus/hotstuff/pacemaker.h"
#include "consensus/hotstuff/block_acceptor.h"
#include "consensus/hotstuff/block_wrapper.h"
#include <consensus/hotstuff/hotstuff.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/zbft/contract_call.h>
#include <consensus/zbft/contract_create_by_root_from_tx_item.h>
#include <consensus/zbft/contract_create_by_root_to_tx_item.h>
#include <consensus/zbft/contract_user_call.h>
#include <consensus/zbft/contract_user_create_call.h>
#include <consensus/zbft/create_library.h>
#include <consensus/zbft/cross_tx_item.h>
#include <consensus/zbft/elect_tx_item.h>
#include <consensus/zbft/from_tx_item.h>
#include <consensus/zbft/join_elect_tx_item.h>
#include <consensus/zbft/pool_statistic_tag.h>
#include <consensus/zbft/root_cross_tx_item.h>
#include <consensus/zbft/root_to_tx_item.h>
#include <consensus/zbft/statistic_tx_item.h>
#include <consensus/zbft/time_block_tx.h>
#include <consensus/zbft/to_tx_item.h>
#include <consensus/zbft/to_tx_local_item.h>
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
#include <protos/view_block.pb.h>
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
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
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
        BlockCacheCallback new_block_cache_callback);
    void OnNewElectBlock(
        uint64_t block_tm_ms,
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& sec_key);
    void OnTimeBlock(
            uint64_t lastest_time_block_tm,
            uint64_t latest_time_block_height,
            uint64_t vss_random) {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            chain(i)->OnTimeBlock(lastest_time_block_tm, latest_time_block_height, vss_random);
        }
    }
    
    HotstuffManager();
    virtual ~HotstuffManager();
    
    void UpdateStoredToDbView(uint32_t pool_index, View view) {
        pool_hotstuff_[pool_index]->UpdateStoredToDbView(view);
    }

    Status Start();
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);

    // std::shared_ptr<ViewBlock> GetViewBlock(uint32_t pool_index, uint64_t view) {
    //     return pool_hotstuff_[pool_index]->GetViewBlock(view);
    // }
    
    void SetSyncPoolFn(SyncPoolFn sync_fn) {
        for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
            pacemaker(pool_idx)->SetSyncPoolFn(sync_fn);
            hotstuff(pool_idx)->SetSyncPoolFn(sync_fn);
        }        
    }

    int VerifySyncedViewBlock(const view_block::protobuf::ViewBlockItem& pb_vblock);    

    inline std::shared_ptr<Hotstuff> hotstuff(uint32_t pool_idx) const {
        auto it = pool_hotstuff_.find(pool_idx);
        if (it == pool_hotstuff_.end()) {
            return nullptr;
        }
        return it->second;
    }    
    
    inline std::shared_ptr<Pacemaker> pacemaker(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->pacemaker();
    }

    inline std::shared_ptr<ViewBlockChain> chain(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->view_block_chain();
    }

    inline std::shared_ptr<IBlockAcceptor> acceptor(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->acceptor();
    }

#ifdef USE_AGG_BLS
    inline std::shared_ptr<AggCrypto> crypto(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->crypto();        
    }    
#else
    inline std::shared_ptr<Crypto> crypto(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->crypto();        
    }
#endif
    
    inline std::shared_ptr<ElectInfo> elect_info() const {
        return elect_info_;
    }

    inline std::shared_ptr<IBlockWrapper> block_wrapper(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->wrapper();   
    }

    void ConsensusAddTxsMessage(const transport::MessagePtr& msg_ptr) {
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        consensus_add_tx_msgs_[thread_idx].push(msg_ptr);
        pop_tx_con_.notify_one();
    }

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void HandleTimerMessage(const transport::MessagePtr& msg_ptr);
    void RegisterCreateTxCallbacks();
    Status VerifyViewBlockWithCommitQC(const view_block::protobuf::ViewBlockItem& pb_vblock);
    void PopPoolsMessage();
    pools::TxItemPtr CreateFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<FromTxItem>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxItem>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateStatisticTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<StatisticTxItem>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateToTxLocal(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxLocalItem>(
                msg_ptr, -1, db_, gas_prepayment_, 
                account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateTimeblockTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<TimeBlockTx>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateRootToTxItem(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<RootToTxItem>(
                elect_info()->max_consensus_sharding_id(),
                msg_ptr, -1,
                vss_mgr_,
                account_mgr_,
                security_ptr_,
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreateLibraryTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<CreateLibrary>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateElectTx(const transport::MessagePtr& msg_ptr) {
        if (first_timeblock_timestamp_ == 0) {
            uint64_t height = 0;
            prefix_db_->GetGenesisTimeblock(&height, &first_timeblock_timestamp_);
        }

        return std::make_shared<ElectTxItem>(
                msg_ptr, -1,
                account_mgr_,
                security_ptr_,
                prefix_db_,
                elect_mgr_,
                vss_mgr_,
                bls_mgr_,
                first_timeblock_timestamp_,
                false,
                elect_info()->max_consensus_sharding_id() - 1,
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreateJoinElectTx(const transport::MessagePtr& msg_ptr) {
        auto keypair = bls::AggBls::Instance()->GetKeyPair();
        if (keypair == nullptr || !keypair->IsValid()) {
            return nullptr;
        }

        return std::make_shared<JoinElectTxItem>(
                msg_ptr, -1, 
                account_mgr_, 
                security_ptr_, 
                prefix_db_, 
                elect_mgr_, 
                msg_ptr->address_info,
                msg_ptr->header.tx_proto().pubkey(),
                keypair->pk(),
                keypair->proof());
    }

    pools::TxItemPtr CreateCrossTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<CrossTxItem>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateRootCrossTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<RootCrossTxItem>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateContractUserCreateCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCreateCall>(
                contract_mgr_, 
                db_, 
                msg_ptr, -1, 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

	pools::TxItemPtr CreateContractByRootFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCreateByRootFromTxItem>(
                contract_mgr_, 
                db_, 
                msg_ptr, -1, 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

	pools::TxItemPtr CreateContractByRootToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCreateByRootToTxItem>(
                contract_mgr_, 
                db_, 
                msg_ptr, -1, 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreateContractUserCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCall>(
                db_, msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateContractCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCall>(
                contract_mgr_, 
                gas_prepayment_, 
                db_, 
                msg_ptr, -1, 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreatePoolStatisticTagTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<PoolStatisticTag>(
                msg_ptr, -1, account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    static const uint64_t kHandleTimerPeriodMs = 3000lu;

    std::unordered_map<uint32_t, std::shared_ptr<Hotstuff>> pool_hotstuff_;
    std::shared_ptr<ElectInfo> elect_info_;
    

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    double prev_tps_[common::kInvalidPoolIndex];
    
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    uint64_t prev_handler_timer_tm_ms_ = 0;
    uint64_t prev_check_timer_single_tm_ms_[common::kImmutablePoolSize] = {0};
    uint64_t first_timeblock_timestamp_ = 0;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    common::ThreadSafeQueue<transport::MessagePtr> consensus_add_tx_msgs_[common::kMaxThreadCount];
    std::shared_ptr<std::thread> pop_message_thread_ = nullptr;
    volatile bool destroy_ = false;
    std::condition_variable pop_tx_con_;
    std::mutex pop_tx_mu_;

    DISALLOW_COPY_AND_ASSIGN(HotstuffManager);
};

}  // namespace consensus

}  // namespace shardora
