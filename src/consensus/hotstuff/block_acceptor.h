#pragma once

#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
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
class IBlockAcceptor {
public:
    // proposed block info
    struct blockInfo {
        View view;
        std::shared_ptr<block::protobuf::Block> block;
        pools::protobuf::StepType tx_type;
        std::vector<const pools::protobuf::TxMessage*> txs;
    };

    // synced block info
    struct blockInfoSync {
        View view;
        std::shared_ptr<block::protobuf::Block> block;
    };
    
    IBlockAcceptor() = default;
    virtual ~IBlockAcceptor() {};

    // Accept a block and txs in it from propose msg.
    virtual Status Accept(
        std::shared_ptr<blockInfo>&, 
        const bool& no_tx_allowed) = 0;
    // Accept a block and txs in it from sync msg.
    virtual Status AcceptSync(const std::shared_ptr<block::protobuf::Block>& block) = 0;
    // Commit a block
    virtual Status Commit(std::shared_ptr<block::protobuf::Block>&) = 0;
    // Add txs to local pool
    virtual Status AddTxs(const std::vector<const pools::protobuf::TxMessage*>& txs) = 0;
    // Return block txs to pool
    virtual Status Return(const std::shared_ptr<block::protobuf::Block>&) = 0;
    // Handle Synced Block From KeyValueSyncer
    virtual void CommitSynced(std::shared_ptr<block::protobuf::Block>& block_ptr) = 0;
    virtual void MarkBlockTxsAsUsed(const std::shared_ptr<block::protobuf::Block>&) = 0;
    virtual double Tps() = 0;
};

class BlockAcceptor : public IBlockAcceptor {
public:
    using TxsFunc = std::function<Status(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&)>;
    
    BlockAcceptor(
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
            consensus::BlockCacheCallback new_block_cache_callback);
    ~BlockAcceptor();

    BlockAcceptor(const BlockAcceptor&) = delete;
    BlockAcceptor& operator=(const BlockAcceptor&) = delete;

    // Accept a proposed block and exec txs in it.
    Status Accept(
        std::shared_ptr<IBlockAcceptor::blockInfo>& blockInfo, 
        const bool& no_tx_allowed) override;
    // Accept a synced block.
    Status AcceptSync(const std::shared_ptr<block::protobuf::Block>& block) override;
    // Commit a block and execute its txs.
    Status Commit(std::shared_ptr<block::protobuf::Block>& block) override;
    // Add txs from hotstuff msg to local pool
    Status AddTxs(const std::vector<const pools::protobuf::TxMessage*>& txs) override;
    // Return expired or invalid block txs to pool
    Status Return(const std::shared_ptr<block::protobuf::Block>& block) override {
        // return txs to the pool
        for (uint32_t i = 0; i < uint32_t(block->tx_list().size()); i++) {
            auto& gid = block->tx_list(i).gid();
            pools_mgr_->RecoverTx(pool_idx_, gid);
        }
        return Status::kSuccess;
    }

    void CommitSynced(std::shared_ptr<block::protobuf::Block>& block_ptr) override;
    // 将 block txs 从交易池中取出，当 block 成功加入链中后调用
    void MarkBlockTxsAsUsed(const std::shared_ptr<block::protobuf::Block>&) override;

    inline double Tps() override {
        return cur_tps_;
    }
    
private:
    uint32_t pool_idx_;
    std::shared_ptr<consensus::WaitingTxsPools> tx_pools_ = nullptr;
    std::unordered_map<pools::protobuf::StepType, TxsFunc> txs_func_map_;
    zjcvm::ZjchainHost zjc_host;
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

    std::map<uint64_t, std::shared_ptr<block::protobuf::Block>> waiting_blocks_[common::kInvalidPoolIndex];

    inline uint32_t pool_idx() const {
        return pool_idx_;
    }

    Status addTxsToPool(
        const std::vector<const pools::protobuf::TxMessage*>& txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr);
    
    bool IsBlockValid(const std::shared_ptr<block::protobuf::Block>&);
    
    Status DoTransactions(
            const std::shared_ptr<consensus::WaitingTxsItem>&,
            std::shared_ptr<block::protobuf::Block>&);

    Status GetAndAddTxsLocally(
            const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
            std::shared_ptr<consensus::WaitingTxsItem>&);

    void LeaderBroadcastBlock(const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastBlock(uint32_t des_shard, const std::shared_ptr<block::protobuf::Block>& block_item);
    void BroadcastLocalTosBlock(const std::shared_ptr<block::protobuf::Block>& block_item);

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
                prev_count_ = 0;
                ZJC_INFO("pool: %d, tps: %.2f", pool_idx_, cur_tps_);
            }
        }        
    }
};

} // namespace hotstuff

} // namespace shardora

