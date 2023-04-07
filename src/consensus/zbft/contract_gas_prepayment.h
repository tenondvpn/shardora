#pragma once

#include "common/unique_map.h"
#include "block/account_manager.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class ContractGasPrepayment {
public:
    ContractGasPrepayment(
            uint8_t thread_count,
            std::shared_ptr<db::Db>& db) {
        thread_count_ = thread_count;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
        prepayment_gas_ = new common::UniqueMap<std::string, uint64_t, 10240, 16>[thread_count];
    }

    virtual ~ContractGasPrepayment() {}

    void NewBlockWithTx(
            uint8_t thread_idx,
            const std::shared_ptr<block::protobuf::Block>& block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        if (tx.step() != pools::protobuf::kContractUserCreateCall) {
            return;
        }

        if (tx.contract_prepayment() <= 0) {
            return;
        }

        if (block_item->height() <= pools_max_heights_[block_item->pool_index()]) {
            return;
        }

        prefix_db_->SaveContractUserPrepayment(
            tx.to(),
            tx.from(),
            block_item->height(),
            tx.contract_prepayment());
        std::string key = tx.to() + tx.from();
        prepayment_gas_[thread_idx].update(key, tx.contract_prepayment());
        pools_max_heights_[block_item->pool_index()] = block_item->height();
    }
  
    uint64_t GetAddressPrepayment(
            uint8_t thread_idx,
            uint32_t pool_index,
            const std::string& contract_addr,
            const std::string& user_addr) {
        assert(thread_idx < thread_count_);
        std::string key = contract_addr + user_addr;
        uint64_t prepayment = 0;
        if (prepayment_gas_[thread_idx].get(key, &prepayment)) {
            return prepayment;
        }

        uint64_t height = 0;
        if (!prefix_db_->GetContractUserPrepayment(contract_addr, user_addr, &height, &prepayment)) {
            prepayment_gas_[thread_idx].add(key, prepayment);
            return 0;
        }

        pools_max_heights_[pool_index] = height;
        return prepayment;
    }

private:
    uint8_t thread_count_ = 0;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::UniqueMap<std::string, uint64_t, 10240, 16>* prepayment_gas_ = nullptr;
    uint64_t pools_max_heights_[common::kImmutablePoolSize] = { 0 };

    DISALLOW_COPY_AND_ASSIGN(ContractGasPrepayment);
};

};  // namespace consensus

};  // namespace zjchain
