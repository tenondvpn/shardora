#include "contract/contract_manager.h"

#include "network/route.h"
#include "network/universal_manager.h"
#include "contract/contract_ecrecover.h"
#include "contract/contract_sha256.h"
#include "contract/contract_ripemd160.h"
#include "contract/contract_identity.h"
#include "contract/contract_modexp.h"
#include "contract/contract_alt_bn128_G1_add.h"
#include "contract/contract_alt_bn128_G1_mul.h"
#include "contract/contract_alt_bn128_pairing_product.h"
#include "contract/contract_blake2_compression.h"

namespace zjchain {

namespace contract {

ContractManager* ContractManager::Instance() {
    static ContractManager ins;
    return &ins;
}

ContractManager::ContractManager() {}

ContractManager::~ContractManager() {}

int ContractManager::Init(std::shared_ptr<security::Security>& secptr) {
    auto ecrecover = std::make_shared<Ecrecover>("", secptr);
    auto contract_sha256 = std::make_shared<ContractSha256>("");
    auto contract_rip160 = std::make_shared<Ripemd160>("");
    auto contract_identity = std::make_shared<Identity>("");
    auto modexp = std::make_shared<Modexp>("");
    auto alt_add = std::make_shared<ContractAltBn128G1Add>("");
    auto alt_mul = std::make_shared<ContractAltBn128G1Mul>("");
    auto alt_product = std::make_shared<ContractaltBn128PairingProduct>("");
    auto blake2 = std::make_shared<Blake2Compression>("");
    contract_map_[kContractEcrecover] = ecrecover;
    contract_map_[kContractSha256] = contract_sha256;
    contract_map_[kContractRipemd160] = contract_rip160;
    contract_map_[kContractIdentity] = contract_identity;
    contract_map_[kContractModexp] = modexp;
    contract_map_[kContractAlt_bn128_G1_add] = alt_add;
    contract_map_[kContractAlt_bn128_G1_mul] = alt_mul;
    contract_map_[kContractAlt_bn128_pairing_product] = alt_product;
    contract_map_[kContractBlake2_compression] = blake2;

    return kContractSuccess;
}

int ContractManager::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    ContractInterfacePtr contract_ptr = nullptr;
    auto iter = contract_map_.find(param.code_address);
    if (iter != contract_map_.end()) {
        contract_ptr = iter->second;
    }

    if (contract_ptr != nullptr) {
        return contract_ptr->call(param, gas, origin_address, res);
    }

    return kContractNotExists;
}

}  // namespace contract

}  // namespace zjchain
