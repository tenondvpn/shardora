#pragma once

#include <vector>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "db/db.h"
#include "pki/pki_cl_agka.h"
#include "pki/pki_cl_param.h"

namespace shardora {

namespace contract {

class ContractCl : public ContractInterface {
public:
    ContractCl() : ContractInterface("") {
        protocol_ = std::make_shared<pkicl::PkiClAgka>(
            pkicl::kTypeA, 
            "79a31ee3205870b7ef3d7aa7d7dc0ae17ce3de24", 
            "0efe323469954d466f958fd2ef66d624b6180be09a8315084278e13cd3903dd8affd0696e3cd829ff7cd010bbd70c2135821ac485dca8ec55bfef92e59164f06283e21a5104844d2b605331efea6e8673ef9d3af502d63a5ab17ad416e393bb8a66200e08c071e0353b094308c1fd8470e7d0c355af41cd66cb531223972c1ab");
    }

    virtual ~ContractCl();
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
        return protocol_->ClExtract(param, key, value);
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
    std::shared_ptr<pkicl::PkiClAgka> protocol_;

    DISALLOW_COPY_AND_ASSIGN(ContractCl);
};

}  // namespace contract

}  // namespace shardora
