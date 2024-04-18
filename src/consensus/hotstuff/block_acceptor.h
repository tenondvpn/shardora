#pragma once

#include <block/account_manager.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/zbft/contract_gas_prepayment.h>
#include <consensus/zbft/waiting_txs_pools.h>
#include <functional>
#include <protos/block.pb.h>
#include <protos/pools.pb.h>

namespace shardora {

namespace hotstuff {

// Block 及 Txs 处理模块
class IBlockAcceptor {
public:
    // the block info struct used in BlockAcceptor
    struct blockInfo {
        View view;
        std::shared_ptr<block::protobuf::Block> block;
        pools::protobuf::StepType tx_type;
        std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    };
    
    IBlockAcceptor() = default;
    virtual ~IBlockAcceptor() {};

    // Accept a block and txs in it.
    virtual Status Accept(std::shared_ptr<blockInfo>&) = 0;
private:
    
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
            const std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    ~BlockAcceptor();

    BlockAcceptor(const BlockAcceptor&) = delete;
    BlockAcceptor& operator=(const BlockAcceptor&) = delete;

    Status Accept(std::shared_ptr<IBlockAcceptor::blockInfo>& blockInfo) override;
private:
    uint32_t pool_idx_;
    std::shared_ptr<consensus::WaitingTxsPools> tx_pools_ = nullptr;
    std::unordered_map<pools::protobuf::StepType, TxsFunc> txs_func_map_;
    zjcvm::ZjchainHost zjc_host;
    std::shared_ptr<db::DbWriteBatch> db_batch_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;

    Status GetTxsFromLocal(
            const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
            std::shared_ptr<consensus::WaitingTxsItem>&);

    bool IsBlockValid(const std::shared_ptr<block::protobuf::Block>&);
    Status DoTransactions(
            const std::shared_ptr<consensus::WaitingTxsItem>&,
            std::shared_ptr<block::protobuf::Block>&);

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

    inline uint32_t pool_idx() const {
        return pool_idx_;
    }  
};

} // namespace hotstuff

} // namespace shardora

