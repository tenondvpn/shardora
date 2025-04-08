#pragma once
#include <common/global_info.h>
#include "consensus/hotstuff/hotstuff_utils.h"
#include <consensus/hotstuff/types.h>
#include <consensus/zbft/waiting_txs.h>
#include <network/network_utils.h>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

// Block 与交易执行者
class IBlockExecutor {
public:
    IBlockExecutor() = default;
    virtual ~IBlockExecutor() {};

    // 执行交易并将结果放入 Block
    virtual Status DoTransactionAndCreateTxBlock(
            const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
            view_block::protobuf::ViewBlockItem* view_block,
            BalanceAndNonceMap& balance_map,
            zjcvm::ZjchainHost& zjc_host) = 0;
};

class ShardBlockExecutor : public IBlockExecutor {
public:
    explicit ShardBlockExecutor(const std::shared_ptr<security::Security>& security) {
        db_batch_ = std::make_shared<db::DbWriteBatch>();
        security_ptr_ = security;
    };
    ~ShardBlockExecutor() {};

    ShardBlockExecutor(const ShardBlockExecutor&) = delete;
    ShardBlockExecutor& operator=(const ShardBlockExecutor&) = delete;

    Status DoTransactionAndCreateTxBlock(
            const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
            view_block::protobuf::ViewBlockItem* view_block,
            BalanceAndNonceMap& balance_map,
            zjcvm::ZjchainHost& zjc_host);
private:
    std::shared_ptr<db::DbWriteBatch> db_batch_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;    
};

class RootBlockExecutor : public IBlockExecutor {
public:
    explicit RootBlockExecutor(const std::shared_ptr<security::Security>& security) {
        db_batch_ = std::make_shared<db::DbWriteBatch>();
        security_ptr_ = security;  
    };
    ~RootBlockExecutor() {};

    RootBlockExecutor(const RootBlockExecutor&) = delete;
    RootBlockExecutor& operator=(const RootBlockExecutor&) = delete;

    Status DoTransactionAndCreateTxBlock(
            const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
            view_block::protobuf::ViewBlockItem* view_block,
            BalanceAndNonceMap& balance_map,
            zjcvm::ZjchainHost& zjc_host);
private:
    std::shared_ptr<db::DbWriteBatch> db_batch_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;

    void RootDefaultTx(
            const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
            view_block::protobuf::ViewBlockItem* view_block,
            BalanceAndNonceMap& balance_map,
            zjcvm::ZjchainHost& zjc_host);
    void RootCreateAccountAddressBlock(
            const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
            view_block::protobuf::ViewBlockItem* view_block,
            BalanceAndNonceMap& balance_map,
            zjcvm::ZjchainHost& zjc_host);
    void RootCreateElectConsensusShardBlock(
            const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
            view_block::protobuf::ViewBlockItem* view_block,
            BalanceAndNonceMap& balance_map,
            zjcvm::ZjchainHost& zjc_host);        
};

class BlockExecutorFactory {
public:
    BlockExecutorFactory() = default;
    ~BlockExecutorFactory() {}

    std::shared_ptr<IBlockExecutor> Create(const std::shared_ptr<security::Security>& security) {
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            return std::make_shared<RootBlockExecutor>(security); 
        }
        return std::make_shared<ShardBlockExecutor>(security);
    }
};

} // namespace hotstuff

} // namespace shardora

