#pragma once

#include <block/account_manager.h>
#include <block/block_manager.h>
#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/zbft/contract_gas_prepayment.h>
#include <consensus/zbft/waiting_txs_pools.h>
#include <dht/dht_key.h>
#include <functional>
#include <network/route.h>
#include <pools/tx_pool_manager.h>
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

namespace consensus {
class ContractGasPrepayment;
}

namespace pools {
class TxPoolManager;
}

namespace block {
class BlockManager;
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
        std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    };

    // synced block info
    struct blockInfoSync {
        View view;
        std::shared_ptr<block::protobuf::Block> block;
    };
    
    IBlockAcceptor() = default;
    virtual ~IBlockAcceptor() {};

    // Accept a block and txs in it from propose msg.
    virtual Status Accept(std::shared_ptr<blockInfo>&) = 0;
    // Accept a block and txs in it from sync msg.
    virtual Status AcceptSync(const std::shared_ptr<blockInfoSync>&) = 0;
    // Commit a block
    virtual Status Commit(std::shared_ptr<block::protobuf::Block>&) = 0;
    // Fetch local txs to send
    virtual Status FetchTxsFromPool(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>) = 0;
    // Add txs to local pool
    virtual Status AddTxsToPool(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>) = 0;
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

    // Accept a block and exec txs in it.
    Status Accept(std::shared_ptr<IBlockAcceptor::blockInfo>& blockInfo) override;
    // Accept a block and txs in it from sync msg.
    Status AcceptSync(const std::shared_ptr<blockInfoSync>& blockInfoSync) override;
    // Commit a block and execute its txs.
    Status Commit(std::shared_ptr<block::protobuf::Block>& block) override;
    // Fetch local txs to send
    Status FetchTxsFromPool(std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs) override;
    // Add txs to local pool
    Status AddTxsToPool(std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs) override;
    
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

    inline uint32_t pool_idx() const {
        return pool_idx_;
    }

    Status addTxsToPool(
        std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr);
    
    bool IsBlockValid(const std::shared_ptr<block::protobuf::Block>&);
    // 将 block txs 从交易池中取出
    void MarkBlockTxsAsUsed(const std::shared_ptr<block::protobuf::Block>&);
    
    Status DoTransactions(
            const std::shared_ptr<consensus::WaitingTxsItem>&,
            std::shared_ptr<block::protobuf::Block>&);

    Status GetTxsFromLocal(
            const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
            std::shared_ptr<consensus::WaitingTxsItem>&);    

    Status GetDefaultTxs(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&);
    Status GetToTxs(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&);
    Status GetStatisticTxs(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&);
    Status GetCrossTxs(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&);
    Status GetElectTxs(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&);
    Status GetTimeBlockTxs(
            const std::shared_ptr<IBlockAcceptor::blockInfo>&,
            std::shared_ptr<consensus::WaitingTxsItem>&);

    void RegisterTxsFunc(pools::protobuf::StepType tx_type, TxsFunc txs_func) {
        txs_func_map_[tx_type] = txs_func;
    }

    TxsFunc GetTxsFunc(pools::protobuf::StepType tx_type) {
        auto it = txs_func_map_.find(tx_type);
        if (it != txs_func_map_.end()) {
            return it->second;
        }
        return std::bind(
                &BlockAcceptor::GetDefaultTxs,
                this,
                std::placeholders::_1,
                std::placeholders::_2);
    }

    void LeaderBroadcastBlock(const std::shared_ptr<block::protobuf::Block>& block);
    void BroadcastBlock(uint32_t des_shard, const std::shared_ptr<block::protobuf::Block>& block_item);
    void BroadcastLocalTosBlock(const std::shared_ptr<block::protobuf::Block>& block_item);    
};

} // namespace hotstuff

} // namespace shardora

