#pragma once

#include <vector>

#include "common/tick.h"
#include "contract/contract_interface.h"
#include "cpppbc/PBC.h"
#include "db/db.h"

namespace shardora {

namespace contract {

class ContractReEncryption : public ContractInterface {
public:
    ContractReEncryption() : ContractInterface("") {
        std::string param = ("type a\n"
        "q 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\n"
        "h 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\n"
        "r 730750818665451621361119245571504901405976559617\n"
        "exp2 159\n"
        "exp1 107\n"
        "sign1 1\n"
        "sign0 1\n");
        pairing_ptr_ = std::make_shared<Pairing>(param.c_str(), param.size());
    }

    virtual ~ContractReEncryption();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int CreatePrivateAndPublicKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int CreateReEncryptionKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int EncryptUserMessage(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int ReEncryptUserMessage(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int ReEncryptUserMessageWithMember(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value);
    int Decryption(const CallParameters& param, const std::string& key, const std::string& value);

private:
    std::shared_ptr<Pairing> pairing_ptr_;

    DISALLOW_COPY_AND_ASSIGN(ContractReEncryption);
};

}  // namespace contract

}  // namespace shardora
