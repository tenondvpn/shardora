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
            const view_block::protobuf::ViewBlockItem& view_block,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        auto& block = view_block.block_info();
        if (tx.status() != consensus::kConsensusSuccess) {
            ZJC_DEBUG("tx.status() != consensus::kConsensusSuccess, gid: %s, from: %s, to: %s", 
                common::Encode::HexEncode(tx.gid()).c_str(), 
                common::Encode::HexEncode(tx.from()).c_str(),
                common::Encode::HexEncode(tx.to()).c_str());
            return;
        }

        const std::string* to_txs_str = nullptr;
        for (int32_t i = 0; i < tx.storages_size(); ++i) {
            ZJC_DEBUG("handle key: %s", tx.storages(i).key().c_str());
            if (tx.storages(i).key() == protos::kConsensusLocalNormalTos) {
                to_txs_str = &tx.storages(i).value();
                break;
            }

            if (tx.storages(i).key() == protos::kNormalToShards) {
                to_txs_str = &tx.storages(i).value();
                pools::protobuf::ToTxMessage to_tx_msg;
                if (!to_tx_msg.ParseFromString(tx.storages(i).value())) {
                    assert(false);
                }

                ZJC_DEBUG("handle kNormalToShards: %s", ProtobufToJson(to_tx_msg).c_str());
                break;
            }
        }

        if (to_txs_str == nullptr) {
            ZJC_WARN("get local tos info failed!");
            assert(false);
            return;
        }

        block::protobuf::ConsensusToTxs to_txs;
        if (!to_txs.ParseFromString(*to_txs_str)) {
            assert(false);
            return;
        }

        ZJC_DEBUG("handle ConsensusToTxs: %s, to_txs.tos_size(): %u",
            "common::Encode::HexEncode(*to_txs_str).c_str()",
            to_txs.tos_size());
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
            if (to_txs.tos(i).to().size() != security::kUnicastAddressLength * 2) {
                ZJC_INFO("invalid to size: %u, save contract prepayment contract: %s, prepayment: %lu, pool: %u, height: %lu",
                    to_txs.tos(i).to().size(),
                    common::Encode::HexEncode(to_txs.tos(i).to()).c_str(),
                    to_txs.tos(i).balance(),
                    view_block.qc().pool_index(),
                    block.height());
                continue;
            }
            
            prefix_db_->SaveContractUserPrepayment(
                to_txs.tos(i).to(), // 对于 kContractGasPrepayment 交易来说，to 当中已经包含了 from
                "",
                block.height(),
                to_txs.tos(i).balance(),
                db_batch);
            prepayment_gas_[thread_idx].Insert(to_txs.tos(i).to(), to_txs.tos(i).balance());
            ZJC_INFO("success save contract prepayment contract: %s, prepayment: %lu, pool: %u, height: %lu",
                common::Encode::HexEncode(to_txs.tos(i).to()).c_str(),
                to_txs.tos(i).balance(),
                view_block.qc().pool_index(),
                block.height());
        }

        ZJC_INFO("success save contract prepayment contract: %s, "
            "set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(tx.to()).c_str(),
            common::Encode::HexEncode(tx.from()).c_str(),
            tx.contract_prepayment(),
            view_block.qc().pool_index(),
            block.height());
    }

    void HandleUserCreate(
            const view_block::protobuf::ViewBlockItem& view_block,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        auto& block = view_block.block_info();
        if (tx.contract_prepayment() <= 0) {
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
        ZJC_INFO("success save contract prepayment contract: %s, "
            "set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(tx.to()).c_str(),
            common::Encode::HexEncode(tx.from()).c_str(),
            tx.contract_prepayment(),
            view_block.qc().pool_index(),
            block.height());
    }

    void HandleContractExecute(
            const view_block::protobuf::ViewBlockItem& view_block,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        auto& block = view_block.block_info();
        prefix_db_->SaveContractUserPrepayment(
            tx.to(),
            tx.from(),
            block.height(),
            tx.balance(),
            db_batch);
        std::string key = tx.to() + tx.from();
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        prepayment_gas_[thread_idx].Insert(key, tx.balance());
        ZJC_INFO("success save contract prepayment contract: %s, set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(tx.to()).c_str(),
            common::Encode::HexEncode(tx.from()).c_str(),
            tx.balance(),
            view_block.qc().pool_index(),
            block.height());
    }

    void NewBlock(
            const view_block::protobuf::ViewBlockItem& view_block,
            db::DbWriteBatch& db_batch) {
        auto& block = view_block.block_info();
        if (block.height() <= pools_max_heights_[view_block.qc().pool_index()]) {
            ZJC_DEBUG("block.height() <= pools_max_heights_[view_block.qc().pool_index()] "
                " %lu, %lu", 
                block.height(), 
                pools_max_heights_[view_block.qc().pool_index()]);
            ZJC_INFO("failed save contract prepayment pool: %u, height: %lu",
                view_block.qc().pool_index(),
                block.height());
            // assert(false);
            return;
        }

        const auto& tx_list = block.tx_list();
        for (int32_t i = 0; i < tx_list.size(); ++i) {
            NewBlockWithTx(view_block, tx_list[i], db_batch);
        }

        pools_max_heights_[view_block.qc().pool_index()] = block.height();
    }

    void NewBlockWithTx(
            const view_block::protobuf::ViewBlockItem& view_block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        ZJC_DEBUG("new block with tx coming: %lu, %lu, gid: %s, from: %s, to: %s", 
            view_block_item.block_info().height(), 
            pools_max_heights_[view_block_item.qc().pool_index()],
            common::Encode::HexEncode(tx.gid()).c_str(), 
            common::Encode::HexEncode(tx.from()).c_str(),
            common::Encode::HexEncode(tx.to()).c_str());
        if (tx.step() == pools::protobuf::kConsensusLocalTos) { // 增加 prepayment 的交易
            HandleLocalToTx(view_block_item, tx, db_batch);
            return;
        }

        if (tx.step() == pools::protobuf::kContractCreate || tx.step() == pools::protobuf::kCreateLibrary) {
            HandleUserCreate(view_block_item, tx, db_batch);
            return;
        }

        if (tx.step() == pools::protobuf::kContractExcute) {
            HandleContractExecute(view_block_item, tx, db_batch);
            return;
        }

        if (tx.step() == pools::protobuf::kContractCreateByRootTo) {
            HandleUserCreate(view_block_item, tx, db_batch);
            return;
        }
    }
  
    uint64_t GetAddressPrepayment(
            uint32_t pool_index,
            const std::string& contract_addr,
            const std::string& user_addr) {
        assert(false);
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

        ZJC_DEBUG("get contract: %s, set user: %s, prepayment: %lu, pool: %u, height: %lu",
            common::Encode::HexEncode(contract_addr).c_str(),
            common::Encode::HexEncode(user_addr).c_str(),
            prepayment,
            pool_index,
            height);
        ZJC_INFO("success get contract prepayment from db %s, %s, %lu", common::Encode::HexEncode(contract_addr).c_str(), common::Encode::HexEncode(user_addr).c_str(), prepayment);
        return prepayment;
    }

private:
    uint8_t thread_count_ = 0;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::LimitHashMap<std::string, uint64_t, 1024>* prepayment_gas_ = nullptr;
    uint64_t pools_max_heights_[common::kInvalidPoolIndex] = { 0 };

    DISALLOW_COPY_AND_ASSIGN(ContractGasPrepayment);
};

};  // namespace consensus

};  // namespace shardora
