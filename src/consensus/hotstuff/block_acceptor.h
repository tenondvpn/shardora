#pragma once

#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/hotstuff_utils.h>
#include <consensus/zbft/contract_gas_prepayment.h>
#include <consensus/zbft/waiting_txs_pools.h>
#include <dht/dht_key.h>
#include <functional>
#include <network/route.h>
#include <protos/block.pb.h>
#include <protos/pools.pb.h>
#include <timeblock/time_block_manager.h>

namespace shardora {

namespace vss {
class VssManager;
}

namespace contract {
class ContractManager;
}

namespace pools {
class TxPoolManager;
}

namespace consensus {
class ContractGasPrepayment;
}

namespace block {
class BlockManager;
class AccountManager;
}

namespace hotstuff {
// One BlockAcceptor Per Pool
// IBlockAcceptor is for block verification, block committion and block_txs' rollback

class ViewBlockChain;
class IBlockAcceptor {
public:
    // synced block info
    struct blockInfoSync {
        View view;
        std::shared_ptr<block::protobuf::Block> block;
    };
    
    IBlockAcceptor() = default;
    virtual ~IBlockAcceptor() {};

    // Accept a block and txs in it from propose msg.
    virtual Status Accept(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        bool no_tx_allowed,
        bool directly_user_leader_txs,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host) = 0;
    // Accept a block and txs in it from sync msg.
    virtual Status AcceptSync(const view_block::protobuf::ViewBlockItem& block) = 0;
    // Commit a block
    virtual void Commit(transport::MessagePtr msg_ptr, std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) = 0;
    // Handle Synced Block From KeyValueSyncer
    virtual void CommitSynced(std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) = 0;
    virtual double Tps() = 0;
};

class BlockAcceptor : public IBlockAcceptor {
public:
    BlockAcceptor();
    ~BlockAcceptor();

    void Init(
        const uint32_t& pool_idx,
        const std::shared_ptr<security::Security>& security,
        const std::shared_ptr<block::AccountManager>& account_mgr,
        const std::shared_ptr<ElectInfo>& elect_info,
        const std::shared_ptr<vss::VssManager>& vss_mgr,
        const std::shared_ptr<contract::ContractManager>& contract_mgr,
        const std::shared_ptr<db::Db>& db,
        const std::shared_ptr<consensus::ContractGasPrepayment>& gas_prepayment,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<elect::ElectManager> elect_mgr,
        consensus::BlockCacheCallback new_block_cache_callback,
        std::shared_ptr<ViewBlockChain> view_block_chain);

    BlockAcceptor(const BlockAcceptor&) = delete;
    BlockAcceptor& operator=(const BlockAcceptor&) = delete;
    // Accept a proposed block and exec txs in it.
    Status Accept(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        bool no_tx_allowed,
        bool directly_user_leader_txs,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host) override;
    // Accept a synced block.
    Status AcceptSync(const view_block::protobuf::ViewBlockItem& block) override;
    // Commit a block and execute its txs.
    void Commit(transport::MessagePtr msg_ptr, std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) override;
    // Add txs from hotstuff msg to local pool
    // Status AddTxs(
    //     transport::MessagePtr msg_ptr, 
    //     const google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>& txs) override;
    void CommitSynced(std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) override;

    inline double Tps() override {
        return cur_tps_;
    }

    private:
    Status addTxsToPool(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>& txs,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        BalanceAndNonceMap& now_balance_map,
        zjcvm::ZjchainHost& zjc_host);
    bool IsBlockValid(const view_block::protobuf::ViewBlockItem&);
    Status DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>&,
        view_block::protobuf::ViewBlockItem*,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host);
    Status GetAndAddTxsLocally(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const hotstuff::protobuf::TxPropose& block_info,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>&,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host);
    void commit(
        transport::MessagePtr msg_ptr, 
        std::shared_ptr<block::BlockToDbItem>& queue_item_ptr);

    void CalculateTps(uint64_t tx_list_size) {
        auto now_tm_us = common::TimeUtils::TimestampUs();
        if (prev_tps_tm_us_ == 0) {
            prev_tps_tm_us_ = now_tm_us;
        }

        {
            common::AutoSpinLock auto_lock(prev_count_mutex_);
            prev_count_ += tx_list_size;
            if (now_tm_us > prev_tps_tm_us_ + 2000000lu) {
                cur_tps_ = (double(prev_count_) / (double(now_tm_us - prev_tps_tm_us_) / 1000000.0)); 
                prev_tps_tm_us_ = now_tm_us;
                if (prev_count_ > 0) {
                    ZJC_WARN("pool: %d, tps: %.2f", pool_idx_, cur_tps_);
                }
                prev_count_ = 0;
            }
        }        
    }

    inline uint32_t pool_idx() const {
        return pool_idx_;
    }

    uint32_t pool_idx_;
    std::shared_ptr<elect::ElectManager> elect_mgr_= nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> tx_pools_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    consensus::BlockCacheCallback new_block_cache_callback_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint64_t prev_tps_tm_us_ = 0;
    uint32_t prev_count_ = 0;
    double cur_tps_ = 0;
    common::SpinMutex prev_count_mutex_;
    std::shared_ptr<ViewBlockChain> view_block_chain_;

};

} // namespace hotstuff

} // namespace shardora

