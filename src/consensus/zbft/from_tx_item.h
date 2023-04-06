#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace consensus {

class FromTxItem : public TxItemBase {
public:
    FromTxItem(
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr)
            : TxItemBase(msg, account_mgr, sec_ptr) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~FromTxItem() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx);

private:
    int NormalTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    int CreateContractUserCall(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    int CreateContractCallExcute(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        evmc::Result* out_res);

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(FromTxItem);
};

};  // namespace consensus

};  // namespace zjchain
