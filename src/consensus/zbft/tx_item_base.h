#pragma once

#include "block/account_manager.h"
#include "pools/tx_pool.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class TxItemBase : public pools::TxItem {
protected:
    TxItemBase(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : pools::TxItem(msg), account_mgr_(account_mgr), sec_ptr_(sec_ptr) {}
    virtual ~TxItemBase() {}

    virtual int HandleTx(
            uint8_t thread_idx,
            const block::protobuf::Block& block,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        return consensus::kConsensusSuccess;
    }

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        DefaultTxItem(tx_info, block_tx);
        // change
        if (tx_info.key().empty()) {
            return consensus::kConsensusError;
        }

        auto storage = block_tx->add_storages();
        storage->set_key(tx_info.key());
        storage->set_val_hash(tx_info.value());
        storage->set_val_size(0);
        return consensus::kConsensusSuccess;
    }

    void DefaultTxItem(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        block_tx->set_gid(tx_info.gid());
        block_tx->set_gas_limit(tx_info.gas_limit());
        block_tx->set_gas_price(tx_info.gas_price());
        block_tx->set_step(tx_info.step());
        block_tx->set_to(tx_info.to());
        block_tx->set_amount(tx_info.amount());
        if (tx_info.step() == pools::protobuf::kContractUserCreateCall ||
                tx_info.step() == pools::protobuf::kContractUserCall) {
            if (tx_info.has_contract_prepayment()) {
                block_tx->set_contract_prepayment(tx_info.contract_prepayment());
            }
        }

        if (tx_info.has_contract_code()) {
            block_tx->set_contract_code(tx_info.contract_code());
        }

        if (tx_info.has_contract_input()) {
            block_tx->set_contract_input(tx_info.contract_input());
        }

        block_tx->set_amount(tx_info.amount());
        block_tx->set_status(kConsensusSuccess);
    }

    int GetTempAccountBalance(
            uint8_t thread_idx,
            const std::string& id,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            uint64_t* balance) {
        auto iter = acc_balance_map.find(id);
        if (iter == acc_balance_map.end()) {
            auto acc_info = account_mgr_->GetAcountInfo(thread_idx, id);
            if (acc_info == nullptr) {
                ZJC_ERROR("account addres not exists[%s]", common::Encode::HexEncode(id).c_str());
                return consensus::kConsensusAccountNotExists;
            }

            acc_balance_map[id] = acc_info->balance();
            *balance = acc_info->balance();
        }
        else {
            *balance = iter->second;
        }

        return kConsensusSuccess;
    }

    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<security::Security> sec_ptr_ = nullptr;
};

};  // namespace consensus

};  // namespace zjchain




