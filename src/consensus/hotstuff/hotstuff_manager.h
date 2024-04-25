#pragma once
#include "consensus/consensus.h"
#include "elect_info.h"
#include "crypto.h"
#include "pacemaker.h"
#include "block_acceptor.h"

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
#include <consensus/zbft/root_cross_tx_item.h>
#include <consensus/zbft/root_to_tx_item.h>
#include <consensus/zbft/statistic_tx_item.h>
#include <consensus/zbft/time_block_tx.h>
#include <consensus/zbft/to_tx_item.h>
#include <consensus/zbft/to_tx_local_item.h>
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
    HotstuffManager();
    virtual ~HotstuffManager();
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    Status VerifyViewBlock(
            const std::shared_ptr<ViewBlock>& v_block,
            const std::shared_ptr<ViewBlockChain>& view_block_chain,
            const uint32_t& elect_height);
    Status Commit(const std::shared_ptr<ViewBlock>& v_block, const uint32_t& pool_index);
    std::shared_ptr<ViewBlock> CheckCommit(
            const std::shared_ptr<ViewBlock>& v_block,
            const uint32_t& pool_index);

    inline std::shared_ptr<Pacemaker> pacemaker(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->pace_maker;
    }

    inline std::shared_ptr<ViewBlockChain> chain(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->view_block_chain;
    }

    inline std::shared_ptr<IBlockAcceptor> acceptor(uint32_t pool_idx) const {
        auto hf = hotstuff(pool_idx);
        if (!hf) {
            return nullptr;
        }
        return hf->block_acceptor;
    }

    inline std::shared_ptr<Crypto> crypto() const {
        return crypto_;
    }

    inline std::shared_ptr<ElectInfo> elect_info() const {
        return elect_info_;
    }

private:

    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void RegisterCreateTxCallbacks();

    void HandleVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index);
    void HandleProposeMsg(const hotstuff::protobuf::ProposeMsg& pro_msg, const uint32_t& pool_index);
    Status SendTranMsg(hotstuff::protobuf::HotstuffMessage& hotstuff_msg);

    Status VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index, 
        std::shared_ptr<ViewBlock>& view_block);

    Status CommitInner(
            const std::shared_ptr<ViewBlockChain>& c,
            const std::shared_ptr<IBlockAcceptor> accp,            
            const std::shared_ptr<ViewBlock>& v_block);

    struct HotStuff
    {
        uint32_t pool_idx;
        std::shared_ptr<Pacemaker> pace_maker;
        std::shared_ptr<IBlockAcceptor> block_acceptor;
        std::shared_ptr<ViewBlockChain> view_block_chain;

        void Init(std::shared_ptr<db::Db>& db_) {
            auto genesis = GetGenesisViewBlock(db_, pool_idx);
            if (genesis) {
                view_block_chain->Store(genesis);
                view_block_chain->SetLatestCommittedBlock(genesis);
                auto sync_info = std::make_shared<SyncInfo>();
                pace_maker->AdvanceView(sync_info->WithQC(genesis->qc));
            } else {
                ZJC_DEBUG("no genesis, pool_idx: %d", pool_idx);
            }
        }
    };

    inline std::shared_ptr<HotStuff> hotstuff(uint32_t pool_idx) const {
        auto it = pool_hotstuff_.find(pool_idx);
        if (it == pool_hotstuff_.end()) {
            return nullptr;
        }
        return std::make_shared<HotStuff>(it->second);
    }
    
    pools::TxItemPtr CreateFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<FromTxItem>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxItem>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateStatisticTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<StatisticTxItem>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateToTxLocal(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ToTxLocalItem>(
                msg_ptr->header.tx_proto(), db_, gas_prepayment_, 
                account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateTimeblockTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<TimeBlockTx>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateRootToTxItem(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<RootToTxItem>(
                elect_info()->max_consensus_sharding_id(),
                msg_ptr->header.tx_proto(),
                vss_mgr_,
                account_mgr_,
                security_ptr_,
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreateLibraryTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<CreateLibrary>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateElectTx(const transport::MessagePtr& msg_ptr) {
        if (first_timeblock_timestamp_ == 0) {
            uint64_t height = 0;
            prefix_db_->GetGenesisTimeblock(&height, &first_timeblock_timestamp_);
        }

        return std::make_shared<ElectTxItem>(
                msg_ptr->header.tx_proto(),
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
        return std::make_shared<JoinElectTxItem>(
                msg_ptr->header.tx_proto(), 
                account_mgr_, 
                security_ptr_, 
                prefix_db_, 
                elect_mgr_, 
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreateCrossTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<CrossTxItem>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateRootCrossTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<RootCrossTxItem>(
                msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateContractUserCreateCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCreateCall>(
                contract_mgr_, 
                db_, 
                msg_ptr->header.tx_proto(), 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

	pools::TxItemPtr CreateContractByRootFromTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCreateByRootFromTxItem>(
                contract_mgr_, 
                db_, 
                msg_ptr->header.tx_proto(), 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

	pools::TxItemPtr CreateContractByRootToTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCreateByRootToTxItem>(
                contract_mgr_, 
                db_, 
                msg_ptr->header.tx_proto(), 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

    pools::TxItemPtr CreateContractUserCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractUserCall>(
                db_, msg_ptr->header.tx_proto(), account_mgr_, security_ptr_, msg_ptr->address_info);
    }

    pools::TxItemPtr CreateContractCallTx(const transport::MessagePtr& msg_ptr) {
        return std::make_shared<ContractCall>(
                contract_mgr_, 
                gas_prepayment_, 
                db_, 
                msg_ptr->header.tx_proto(), 
                account_mgr_, 
                security_ptr_, 
                msg_ptr->address_info);
    }

    std::unordered_map<uint32_t, HotStuff> pool_hotstuff_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<Crypto> crypto_;

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;

    std::atomic<uint32_t> tps_{ 0 };
    std::atomic<uint32_t> pre_tps_{ 0 };
    uint64_t tps_btime_{ 0 };
    
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    BlockCacheCallback new_block_cache_callback_ = nullptr;

    uint64_t first_timeblock_timestamp_ = 0;

    DISALLOW_COPY_AND_ASSIGN(HotstuffManager);
};

}  // namespace consensus

}  // namespace shardora
