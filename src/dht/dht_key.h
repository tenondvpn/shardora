#pragma once

#include <string.h>

#include "dht/dht_utils.h"

namespace zjchain {

namespace dht {

class DhtKeyManager {
public:
    explicit DhtKeyManager(const std::string& str_key);
    DhtKeyManager(uint32_t net_id);
    DhtKeyManager(uint32_t net_id, const std::string& pubkey);
    ~DhtKeyManager();
    const std::string& StrKey();
    static uint32_t DhtKeyGetNetId(const std::string& dht_key);

private:
    common::DhtKey dht_key_;
    std::string str_key_;
};

}  // namespace dht

}  // namespace zjchain
