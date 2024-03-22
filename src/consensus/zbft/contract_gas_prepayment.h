#pragma once

#include "common/global_info.h"
#include "common/limit_hash_map.h"
#include "block/account_manager.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class ContractGasPrepayment {
public:
    ContractGasPrepayment(std::shared_ptr<db::Db>& db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
        prepayment_gas_ = new common::LimitHashMap<std::string, uint64_t, 1024>[common::kMaxThreadCount];
    }

    virtual ~ContractGasPrepayment() {}

    void HandleLocalToTx(
            const block::protobuf::Block& block,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        if (tx.status() != consensus::kConsensusSuccess) {
            return;
        }

        if (block.height() <= pools_max_heights_[block.pool_index()]) {
//             assert(false);
            return;
        }

        std::string to_txs_str;
        for (int32_t i = 0; i < tx.storages_size(); ++i) {
            if (tx.storages(i).key() == protos::kConsensusLocalNormalTos) {
                if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &to_txs_str)) {
                    ZJC_DEBUG("handle local to tx failed get val hash error: %s",
                        common::Encode::HexEncode(tx.storages(i).val_hash()).c_str());
                    assert(false);
                    return;
                }

                break;
            }
        }

        if (to_txs_str.empty()) {
            ZJC_WARN("get local tos info failed!");
            assert(false);
            return;
        }

        block::protobuf::ConsensusToTxs to_txs;
        if (!to_txs.ParseFromString(to_txs_str)) {
            assert(false);
            return;
        }

        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
            if (to_txs.tos(i).to().size() != security::kUnicastAddressLength * 2) {
                continue;
            }
            
            prefix_db_->SaveContractUserPrepayment(
                to_txs.tos(i).to(), // 对于 kContractGasPrepayment 交易来说，to 当中已经包含了 from
                "",
                block.height(),
                to_txs.tos(i).balance(),
                db_batch);
            prepayment_gas_[thread_idx].Insert(to_txs.tos(i).to(), to_txs.tos(i).balance());
            ZJC_DEBUG("success save contract prepayment contract: %s, prepayment: %lu, pool: %u, height: %lu",
                common::Encode::HexEncode(to_txs.tos(i).to()).c_str(),
                to_txs.tos(i).balance(),
                block.pool_index(),
                block.height());
        }

        pools_max_heights_[block.pool_index()] = block.height();
    }

    void HandleUserCreate(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
        if (tx.contract_prepayment() <= 0) {
            return;
        }

        if (block.height() <= pools_max_heights_[block.pool_index()]) {
            return;
        }

        prefix_db_->SaveContractUserPrepayment(
            tx.to(),
            tx.from(),
            block.height(),
            tx.contract_prepayment(),
            db_batch);
        std::string key = tx.to() + tx.from();
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        prepayment_gas_[thread_idx].Insert(key, tx.contract_prepayment());
        pools_max_heights_[block.pool_index()] = block.height();
        ZJC_DEBUG("success save contract prepayment contract: %s, set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(tx.to()).c_str(),
            common::Encode::HexEncode(tx.from()).c_str(),
            tx.contract_prepayment(),
            block.pool_index(),
            block.height());
    }

    void HandleContractExecute(
            const block::protobuf::Block& block,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        if (block.height() <= pools_max_heights_[block.pool_index()]) {
            return;
        }

        prefix_db_->SaveContractUserPrepayment(
            tx.to(),
            tx.from(),
            block.height(),
            tx.balance(),
            db_batch);
        std::string key = tx.to() + tx.from();
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        prepayment_gas_[thread_idx].Insert(key, tx.balance());
        pools_max_heights_[block.pool_index()] = block.height();
        ZJC_DEBUG("success save contract prepayment contract: %s, set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(tx.to()).c_str(),
            common::Encode::HexEncode(tx.from()).c_str(),
            tx.balance(),
            block.pool_index(),
            block.height());
    }

    void NewBlockWithTx(
            const std::shared_ptr<block::protobuf::Block>& block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        if (tx.step() == pools::protobuf::kConsensusLocalTos) { // 增加 prepayment 的交易
            HandleLocalToTx(*block_item, tx, db_batch);
            return;
        }

        if (tx.step() == pools::protobuf::kContractCreate) {
            HandleUserCreate(*block_item, tx, db_batch);
            return;
        }

        if (tx.step() == pools::protobuf::kContractExcute) {
            HandleContractExecute(*block_item, tx, db_batch);
            return;
        }

        if (tx.step() == pools::protobuf::kContractCreateByRootTo) {
            HandleUserCreate(*block_item, tx, db_batch);
            return;
        }
    }
  
    uint64_t GetAddressPrepayment(
            uint32_t pool_index,
            const std::string& contract_addr,
            const std::string& user_addr) {
        std::string key = contract_addr + user_addr;
        uint64_t prepayment = 0;
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        if (prepayment_gas_[thread_idx].Get(key, &prepayment)) {
            ZJC_DEBUG("success get contract prepayment %s, %s, %lu", common::Encode::HexEncode(contract_addr).c_str(), common::Encode::HexEncode(user_addr).c_str(), prepayment);
            return prepayment;
        }

        uint64_t height = 0;
        if (!prefix_db_->GetContractUserPrepayment(contract_addr, user_addr, &height, &prepayment)) {
            prepayment_gas_[thread_idx].Get(key, &prepayment);
            ZJC_DEBUG("failed get contract prepayment %s, %s, %lu", common::Encode::HexEncode(contract_addr).c_str(), common::Encode::HexEncode(user_addr).c_str(), prepayment);
            return 0;
        }

        pools_max_heights_[pool_index] = height;
        ZJC_DEBUG("get contract: %s, set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(contract_addr).c_str(),
            common::Encode::HexEncode(user_addr).c_str(),
            prepayment,
            pool_index,
            height);
        ZJC_DEBUG("success get contract prepayment from db %s, %s, %lu", common::Encode::HexEncode(contract_addr).c_str(), common::Encode::HexEncode(user_addr).c_str(), prepayment);
        return prepayment;
    }

private:
    uint8_t thread_count_ = 0;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::LimitHashMap<std::string, uint64_t, 1024>* prepayment_gas_ = nullptr;
    uint64_t pools_max_heights_[common::kImmutablePoolSize] = { 0 };

    DISALLOW_COPY_AND_ASSIGN(ContractGasPrepayment);
};

};  // namespace consensus

};  // namespace shardora
