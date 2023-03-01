#pragma once

#include "block/account_manager.h"
#include "pools/tx_pool.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class ToTxLocalItem : public pools::TxItem {
public:
    ToTxLocalItem(
            transport::MessagePtr& msg,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr)
            : pools::TxItem(msg), db_(db), account_mgr_(account_mgr), sec_ptr_(sec_ptr) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }


    virtual ~ToTxLocalItem() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx);

private:
    void DefaultTxItem(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        block_tx->set_gid(tx_info.gid());
        block_tx->set_from(sec_ptr_->GetAddress(tx_info.pubkey()));
        block_tx->set_gas_limit(tx_info.gas_limit());
        block_tx->set_gas_price(tx_info.gas_price());
        block_tx->set_step(tx_info.step());
        block_tx->set_to(tx_info.to());
        block_tx->set_amount(tx_info.amount());
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

    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<security::Security> sec_ptr_ = nullptr;
};

};  // namespace consensus

};  // namespace zjchain




