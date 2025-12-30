#pragma once

#include "block/account_manager.h"
#include "consensus/hotstuff/hotstuff_utils.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "pools/tx_pool.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class TxItemBase : public pools::TxItem {
protected:
    TxItemBase(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : pools::TxItem(msg_ptr, tx_index, addr_info), account_mgr_(account_mgr), sec_ptr_(sec_ptr) {}

    virtual ~TxItemBase() {}

    virtual int HandleTx(
            view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        return consensus::kConsensusSuccess;
    }

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        if (!DefaultTxItem(tx_info, block_tx)) {
            return consensus::kConsensusError;
        }

        return consensus::kConsensusSuccess;
    }

    virtual void InitHost(
            zjcvm::ZjchainHost& zjc_host, 
            const block::protobuf::BlockTx& tx, 
            uint64_t gas_limit, 
            uint64_t gas_price, 
            view_block::protobuf::ViewBlockItem& view_block) {
        zjcvm::Uint64ToEvmcBytes32(
            zjc_host.tx_context_.tx_gas_price,
            gas_price);
        zjc_host.contract_mgr_ = contract_mgr_;
        zjc_host.my_address_ = tx.to();
        zjc_host.recorded_selfdestructs_ = nullptr;
        zjc_host.gas_more_ = 0lu;
        zjc_host.create_bytes_code_ = "";
        zjc_host.contract_to_call_dirty_ = false;
        zjc_host.recorded_logs_.clear();
        zjc_host.to_account_value_.clear();
        zjc_host.view_ = view_block.qc().view();
        zjc_host.tx_context_.block_gas_limit = gas_limit;
        zjc_host.tx_context_.block_number = view_block.block_info().height();
        zjc_host.tx_context_.block_timestamp= view_block.block_info().timestamp();
    }

    bool DefaultTxItem(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        block_tx->set_nonce(tx_info.nonce());
        block_tx->set_gas_limit(tx_info.gas_limit());
        block_tx->set_gas_price(tx_info.gas_price());
        block_tx->set_step(tx_info.step());
        block_tx->set_to(tx_info.to());
        block_tx->set_amount(tx_info.amount());
        block_tx->set_unique_hash("");
        if (tx_info.step() == pools::protobuf::kContractCreate ||
            tx_info.step() == pools::protobuf::kCreateLibrary||
            tx_info.step() == pools::protobuf::kContractGasPrepayment ||
            tx_info.step() == pools::protobuf::kRootCreateAddress) {
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
#ifndef NDEBUG
        for (int32_t i = 0; i < tx_info.tx_debug_size(); ++i) {
            auto* tx_debug_info = block_tx->add_tx_debug();
            *tx_debug_info = tx_info.tx_debug(i);
        }
#endif
        return true;
    }

    int GetTempAccountBalance(
            zjcvm::ZjchainHost& zjc_host,
            const std::string& id,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            uint64_t* balance,
            uint64_t* nonce) {
        auto iter = acc_balance_map.find(id);
        if (iter == acc_balance_map.end()) {
            protos::AddressInfoPtr acc_info = zjc_host.view_block_chain_->ChainGetAccountInfo(id);
            if (acc_info == nullptr) {
                SHARDORA_DEBUG("account addres not exists[%s]", common::Encode::HexEncode(id).c_str());
                return consensus::kConsensusAccountNotExists;
            }

            if (acc_info->destructed()) {
                SHARDORA_DEBUG("contract destructed: %s", common::Encode::HexEncode(id).c_str());
                return consensus::kConsensusAccountNotExists;
            }

            acc_balance_map[id] = std::make_shared<address::protobuf::AddressInfo>(*acc_info);
            *balance = acc_info->balance();
            *nonce = acc_info->nonce();
            SHARDORA_DEBUG("success get temp account balance from lru map: %s, balance: %lu, nonce: %lu",
                common::Encode::HexEncode(id).c_str(), *balance, *nonce);
        } else {
            *balance = iter->second->balance();
            *nonce = iter->second->nonce();
            SHARDORA_DEBUG("success get temp account balance from temp map: %s, balance: %lu, nonce: %lu",
                common::Encode::HexEncode(id).c_str(), *balance, *nonce);
        }

        return kConsensusSuccess;
    }

    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<security::Security> sec_ptr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
};

};  // namespace consensus

};  // namespace shardora




