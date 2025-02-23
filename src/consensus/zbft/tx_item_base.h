#pragma once

#include "block/account_manager.h"
#include "pools/tx_pool.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class TxItemBase : public pools::TxItem {
protected:
    TxItemBase(
        const pools::protobuf::TxMessage& tx,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : pools::TxItem(tx, addr_info), account_mgr_(account_mgr), sec_ptr_(sec_ptr) {}

    virtual ~TxItemBase() {}

    virtual int HandleTx(
            const view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
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
            return consensus::kConsensusSuccess;
        }

        auto storage = block_tx->add_storages();
        storage->set_key(tx_info.key());
        storage->set_value(tx_info.value());
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
        if (tx_info.step() == pools::protobuf::kContractCreate ||
            tx_info.step() == pools::protobuf::kContractGasPrepayment ||
            tx_info.step() == pools::protobuf::kContractCreateByRootFrom ||
            tx_info.step() == pools::protobuf::kContractCreateByRootTo ||
            tx_info.step() == pools::protobuf::kRootCreateAddress) {
            if (tx_info.has_contract_prepayment()) {
                block_tx->set_contract_prepayment(tx_info.contract_prepayment());
            }
        }

        // ZJC_DEBUG("gid: %s, contract_code: %d, amount: %d, contract_from: %s",
        //     common::Encode::HexEncode(tx_info.gid()).c_str(),
        //     tx_info.has_contract_code(), tx_info.amount(),
        //     common::Encode::HexEncode(tx_info.contract_from()).c_str());
		
        if (tx_info.has_contract_code()) {
            block_tx->set_contract_code(tx_info.contract_code());
        }

        if (tx_info.has_contract_input()) {
            block_tx->set_contract_input(tx_info.contract_input());
        }

        if (tx_info.has_contract_from()) {
            block_tx->set_from(tx_info.contract_from());
        }

        block_tx->set_amount(tx_info.amount());
        block_tx->set_status(kConsensusSuccess);
#ifndef NDEBUG
        for (int32_t i = 0; i < tx_info.tx_debug_size(); ++i) {
            auto* tx_debug_info = block_tx->add_tx_debug();
            *tx_debug_info = tx_info.tx_debug(i);
        }
#endif
    }

    int GetTempAccountBalance(
            const std::string& id,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            uint64_t* balance) {
        auto iter = acc_balance_map.find(id);
        if (iter == acc_balance_map.end()) {
            auto acc_info = account_mgr_->GetAccountInfo(id);
            if (acc_info == nullptr) {
                ZJC_DEBUG("account addres not exists[%s]", common::Encode::HexEncode(id).c_str());
                return consensus::kConsensusAccountNotExists;
            }

            if (acc_info->destructed()) {
                ZJC_DEBUG("contract destructed: %s", common::Encode::HexEncode(id).c_str());
                return consensus::kConsensusAccountNotExists;
            }

            acc_balance_map[id] = acc_info->balance();
            *balance = acc_info->balance();
            // ZJC_DEBUG("success get temp account balance from account_mgr: %s, %lu",
            //     common::Encode::HexEncode(id).c_str(), *balance);
        } else {
            if (iter->second == -1) {
                return consensus::kConsensusAccountNotExists;
            }

            *balance = iter->second;
            // ZJC_DEBUG("success get temp account balance from tmp balance map: %s, %lu",
            //     common::Encode::HexEncode(id).c_str(), *balance);
        }

        return kConsensusSuccess;
    }

    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<security::Security> sec_ptr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
};

};  // namespace consensus

};  // namespace shardora




