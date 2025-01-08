#pragma once

#include <vector>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "cpppbc/PBC.h"
#include "db/db.h"
#include "pki/pki_ib_agka.h"

namespace shardora {

namespace contract {

class ContractPki : public ContractInterface {
public:
    ContractPki() : ContractInterface("") {
        std::string param = ("(\n"
            "type a\n"
            "q 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\n"
            "h 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\n"
            "r 730750818665451621361119245571504901405976559617\n"
            "exp2 159\n"
            "exp1 107\n"
            "sign1 1\n"
            "sign0 1\n"
            ")");
        protocol_ = std::make_shared<PkiIbAgka>(
            param, 
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
    

private:
    std::shared_ptr<PkiIbAgka> protocol_;

    DISALLOW_COPY_AND_ASSIGN(ContractPki);
};

}  // namespace contract

}  // namespace shardora
