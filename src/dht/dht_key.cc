#include "dht/dht_key.h"

#include <cassert>

#include "common/global_info.h"
#include "common/hash.h"
#include "common/random.h"
#include "common/encode.h"
#include "dht/dht_utils.h"

namespace zjchain {

namespace dht {

DhtKeyManager::DhtKeyManager(const std::string& str_key) {
    assert(str_key.size() == kDhtKeySize);
    memcpy(dht_key_.dht_key, str_key.c_str(), sizeof(dht_key_.dht_key));
    str_key_ = str_key;
}

DhtKeyManager::DhtKeyManager(uint32_t net_id) {
    dht_key_.construct.net_id = net_id;
    memcpy(
        dht_key_.construct.hash,
        common::Random::RandomString(
            sizeof(dht_key_.construct.hash) / sizeof(char)).c_str(),
        sizeof(dht_key_.construct.hash));
    str_key_ = std::string(dht_key_.dht_key, sizeof(dht_key_.dht_key));
}

DhtKeyManager::DhtKeyManager(uint32_t net_id, const std::string& pubkey) {
    dht_key_.construct.net_id = net_id;
    auto hash192 = common::Hash::keccak256(pubkey);
    memcpy(
            dht_key_.construct.hash,
            hash192.c_str(),
            sizeof(dht_key_.construct.hash));
    str_key_ = std::string(dht_key_.dht_key, sizeof(dht_key_.dht_key));
}

DhtKeyManager::~DhtKeyManager() {}

const std::string& DhtKeyManager::StrKey() {
    return str_key_;
}

uint32_t DhtKeyManager::DhtKeyGetNetId(const std::string& dht_key) {
    assert(dht_key.size() == kDhtKeySize);
    auto* cons_key = (common::DhtKey::Construct*)(dht_key.c_str());
    return cons_key->net_id;
}

}  // namespace dht

}  // namespace zjchain
