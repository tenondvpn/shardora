#include "contract/contract_cl.h"

#include "common/split.h"
#include "common/string_utils.h"
#include "common/time_utils.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace contract {

ContractCl::~ContractCl() {}

int ContractCl::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    return kContractSuccess;
}


}  // namespace contract

}  // namespace shardora
