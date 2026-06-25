#pragma once

#include "common/lru_set.h"
#include "common/utils.h"
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/hotstuff_utils.h>
#include <consensus/zbft/waiting_txs_pools.h>
#include <dht/dht_key.h>
#include <network/route.h>
#include <protos/block.pb.h>
#include <protos/pools.pb.h>
#include <timeblock/time_block_manager.h>

#include <atomic>
#include <functional>
#include <vector>

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

namespace block {
class BlockManager;
class AccountManager;
}

namespace bls {
    class BlsManager;
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
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map = nullptr) = 0;
    // Accept a block and txs in it from sync msg.
    virtual Status AcceptSync(const view_block::protobuf::ViewBlockItem& block) = 0;
    // Commit a block
    // Handle Synced Block From KeyValueSyncer
    virtual double Tps() = 0;
    virtual void CalculateTps(uint64_t tx_list_size) = 0;
    virtual bool TxHashVerified(const std::string& tx_hash) = 0;
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
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<elect::ElectManager> elect_mgr,
        std::shared_ptr<ViewBlockChain> view_block_chain,
        std::shared_ptr<bls::BlsManager> bls_mgr);

    bool TxHashVerified(const std::string& tx_hash) {
        return checked_tx_hash_.Push(tx_hash);
    }

    BlockAcceptor(const BlockAcceptor&) = delete;
    BlockAcceptor& operator=(const BlockAcceptor&) = delete;
    // Accept a proposed block and exec txs in it.
    Status Accept(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        bool no_tx_allowed,
        bool directly_user_leader_txs,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map = nullptr) override;
    // Accept a synced block.
    Status AcceptSync(const view_block::protobuf::ViewBlockItem& block) override;

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
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map = nullptr);
    bool IsBlockValid(const view_block::protobuf::ViewBlockItem&);
    Status DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>&,
        view_block::protobuf::ViewBlockItem*,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host);
    Status GetAndAddTxsLocally(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const hotstuff::protobuf::TxPropose& block_info,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>&,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map = nullptr);
    void UpdateDesShardingId(
        pools::protobuf::ToTxMessageItem* to_addr_info, 
        shardoravm::ShardorahainHost& shardora_host);
    
    // Validate statistic transaction node consistency (90% threshold)
    bool ValidateStatisticNodeConsistency(
        const pools::protobuf::ElectStatistic& leader_statistic,
        uint32_t pool_index);

    void CalculateTps(uint64_t tx_list_size) {
        auto now_tm_us = common::TimeUtils::TimestampUs();
        if (prev_tps_tm_us_ == 0) {
            prev_tps_tm_us_ = now_tm_us;
        }

        {
            prev_count_ += tx_list_size;
            if (now_tm_us > prev_tps_tm_us_ + 2000000lu) {
                cur_tps_ = (double(prev_count_) / (double(now_tm_us - prev_tps_tm_us_) / 1000000.0)); 
                prev_tps_tm_us_ = now_tm_us;
                if (prev_count_ > 0) {
                    SHARDORA_WARN("pool: %d, tps: %.2f", pool_idx_, cur_tps_);
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
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::atomic<uint64_t> prev_tps_tm_us_ = 0;
    std::atomic<uint32_t> prev_count_ = 0;
    double cur_tps_ = 0;
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    common::LRUSet<std::string> checked_tx_hash_{10 * common::kMaxTxCount};

    // Set after successful attach to the global tx verify pool in Init().
    bool tx_verify_pool_acquired_ = false;

    // Tx signature verify uses a single process-wide thread pool (shared by all
    // pools); see block_acceptor.cc. RunVerifyBatch forwards to that pool.
    void RunVerifyBatch(std::vector<std::function<void()>>& tasks);

};

} // namespace hotstuff

} // namespace shardora

