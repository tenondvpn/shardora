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
        : pools::TxItem(msg_ptr, tx_index, addr_info), account_mgr_(account_mgr), sec_ptr_(sec_ptr) {
        common::GlobalInfo::Instance()->AddSharedObj(13);
    }

    virtual ~TxItemBase() {
        common::GlobalInfo::Instance()->DecSharedObj(13);
    }

    virtual int HandleTx(
            uint32_t tx_index,
            view_block::protobuf::ViewBlockItem& view_block,
            shardoravm::ShardorahainHost& shardora_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint32_t status_code = 0;
        // shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), std::string((char*)&status_code, sizeof(status_code)));
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
            shardoravm::ShardorahainHost& shardora_host, 
            const block::protobuf::BlockTx& tx, 
            uint64_t gas_limit, 
            uint64_t gas_price, 
            view_block::protobuf::ViewBlockItem& view_block) {
        shardoravm::Uint64ToEvmcBytes32(
            shardora_host.tx_context_.tx_gas_price,
            gas_price);
        shardora_host.contract_mgr_ = contract_mgr_;
        shardora_host.my_address_ = tx.to();
        shardora_host.recorded_selfdestructs_ = nullptr;
        shardora_host.gas_more_ = 0lu;
        shardora_host.create_bytes_code_ = "";
        shardora_host.contract_to_call_dirty_ = false;
        shardora_host.recorded_logs_.clear();
        shardora_host.to_account_value_.clear();
        shardora_host.view_ = view_block.qc().view();
        shardora_host.tx_context_.block_gas_limit = gas_limit;
        shardora_host.tx_context_.block_number = view_block.block_info().height();
        shardora_host.tx_context_.block_timestamp = view_block.block_info().timestamp();
        uint64_t chain_id = hotstuff::kGlobalChainId;
        shardoravm::Uint64ToEvmcBytes32(
            shardora_host.tx_context_.chain_id,
            chain_id);
        SHARDORA_DEBUG("init host, block number: %lu, timestamp: %lu, gas limit: %lu, "
            "gas price: %lu, from: %s, to: %s",
            shardora_host.tx_context_.block_number, shardora_host.tx_context_.block_timestamp,
            shardora_host.tx_context_.block_gas_limit, gas_price,
            common::Encode::HexEncode(tx.from()).c_str(),
            common::Encode::HexEncode(tx.to()).c_str());
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
        if (tx_info.has_tx_hash()) {
            block_tx->set_tx_hash(tx_info.tx_hash());
        } else {
            block_tx->set_tx_hash(pools::GetTxMessageHash(tx_info));
        }

        if (tx_info.step() == pools::protobuf::kCreateContract ||
            tx_info.step() == pools::protobuf::kCreateLibrary||
            tx_info.step() == pools::protobuf::kContractGasPrefund ||
            tx_info.step() == pools::protobuf::kRootCreateAddress) {
            if (tx_info.has_contract_prefund()) {
                block_tx->set_contract_prefund(tx_info.contract_prefund());
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
            shardoravm::ShardorahainHost& shardora_host,
            const std::string& id,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            uint64_t* balance,
            uint64_t* nonce) {
        auto iter = acc_balance_map.find(id);
        if (iter == acc_balance_map.end()) {
            protos::AddressInfoPtr acc_info = shardora_host.view_block_chain_->ChainGetAccountInfo(id);
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




