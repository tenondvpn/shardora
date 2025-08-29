#pragma once

#include <vector>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "db/db.h"
#include "pki/pki_ib_agka.h"
#include "pki/param.h"

namespace shardora {

namespace contract {

class ContractPki : public ContractInterface {
public:
    ContractPki() : ContractInterface("") {
        protocol_ = std::make_shared<pki::PkiIbAgka>(
            pki::kTypeA, 
            "2f8175e95fb7fe128355fce11b1e6a2e4633c284", 
            "7fb3c66433c155c9258475362a92beca9827075a753980e9ff9dde68eea6195676246529939cd086ece99a902dc16ecb275c3b20e6be0b00e470d4dd012a5acd9fd7d4df606f7f3525bb13affe4036e7196366c8c047a73036f68354cd1c611e4eeda601b93abb68888f2f2191f01216c984aaa86b4ade36b84d7bdfca4ffcf2");
        protocol_->Setup();
    }

    virtual ~ContractPki();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
        
    int PkiExtract(
            const shardora::contract::CallParameters& param, 
            const std::string& key, 
            const std::string& value) {
        return protocol_->PkiExtract(param, key, value);
    }

    int IbExtract(
            const shardora::contract::CallParameters& param, 
            const std::string& key, 
            const std::string& value) {
        return protocol_->IbExtract(param, key, value);
    }

    int EncKeyGen(
            const shardora::contract::CallParameters& param, 
            const std::string& key, 
            const std::string& value) {
        return protocol_->EncKeyGen(param, key, value);
    }

    int DecKeyGen(
            const shardora::contract::CallParameters& param, 
            const std::string& key, 
            const std::string& value) {
        return protocol_->DecKeyGen(param, key, value);
    }

    int Enc(
            const shardora::contract::CallParameters& param, 
            const std::string& key, 
            const std::string& value) {
        return protocol_->Enc(param, key, value);
    }

    int Dec(
            const shardora::contract::CallParameters& param, 
            const std::string& key, 
            const std::string& value) {
        return protocol_->Dec(param, key, value);
    }

private:
    std::shared_ptr<pki::PkiIbAgka> protocol_;

    DISALLOW_COPY_AND_ASSIGN(ContractPki);
};

}  // namespace contract

}  // namespace shardora
