#pragma once

#include <string>
#include <memory>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include "common/utils.h"

namespace shardora {

namespace security {

class PrivateKey {
public:
    PrivateKey(const PrivateKey& src);
    PrivateKey(const char* src, uint32_t length);
    explicit PrivateKey(const std::string& src);
    PrivateKey& operator=(const PrivateKey&);
    bool operator==(const PrivateKey& r) const;
    uint32_t Serialize(std::string& dst) const;
    int Deserialize(const std::string& src);

    const char* private_key() const {
        if (private_key_ptr_ != nullptr) {
            return private_key_ptr_;
        }

        return private_key_.c_str();
    }

    const std::shared_ptr<BIGNUM>& bignum() const {
        return bignum_;
    }

private:

    std::shared_ptr<BIGNUM> bignum_;
    std::string private_key_;
    const char* private_key_ptr_ = nullptr;
    uint32_t private_key_length_ = 0;
};

}  // namespace security

}  // namespace shardora
