#pragma once

#include "common/hash.h"
#include "common/utils.h"
#include "protos/pools.pb.h"

namespace zjchain {

namespace pools {

static const uint32_t kBftStartDeltaTime = 500000u;
static const uint32_t kTxPoolTimeoutUs = 30u * 1000u * 1000u;
static const uint32_t kTxStorageKeyMaxSize = 12u;

enum PoolsErrorCode {
    kPoolsSuccess = 0,
    kPoolsError = 1,
    kPoolsTxAdded = 2,
};

struct ToTxItem {
    std::string to_or_txhash;
    uint64_t amount;
};

static inline std::string GetTxMessageHash(const pools::protobuf::TxMessage& tx_info) {
    std::string message;
    message.reserve(tx_info.ByteSizeLong());
    message.append(tx_info.gid());
    message.append(tx_info.pubkey());
    message.append(tx_info.to());
    uint64_t amount = tx_info.amount();
    message.append(std::string((char*)&amount, sizeof(amount)));
    uint64_t gas_limit = tx_info.gas_limit();
    message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = tx_info.gas_price();
    message.append(std::string((char*)&gas_price, sizeof(gas_price)));
    uint64_t step = tx_info.step();
    message.append(std::string((char*)&step, sizeof(step)));
    message.append(tx_info.key());
    message.append(tx_info.value());
    return common::Hash::keccak256(message);
}

};  // namespace pools

};  // namespace zjchain
