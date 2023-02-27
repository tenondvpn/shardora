#pragma once

#include "consensus/zbft/zbft.h"

namespace zjchain {

namespace consensus {

class ContractZbft : public Zbft {
public:
    ContractZbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<WaitingTxsItem>& tx_ptr,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    virtual ~ContractZbft();
    virtual int Init();
    virtual int Start();
    int CallContract(
        pools::TxItemPtr& tx_info,
        evmc::Result* out_res);
    int AddCallContract(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx);
    int CallContractDefault(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx);
    int CallContractExceute(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx);
    int CallContractCalled(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& tx);
    int CreateContractCallExcute(
        pools::TxItemPtr& tx_info,
        uint64_t gas_limit,
        const std::string& bytes_code,
        evmc::Result* out_res);

    DISALLOW_COPY_AND_ASSIGN(ContractZbft);
};

};  // namespace consensus

};  // namespace zjchain