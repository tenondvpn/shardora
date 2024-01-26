#pragma once

#include <string>
#include <memory>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include "common/utils.h"

namespace zjchain {

namespace security {

class PrivateKey {
public:
    PrivateKey(const PrivateKey& src);
    explicit PrivateKey(const std::string& src);
    PrivateKey& operator=(const PrivateKey&);
    bool operator==(const PrivateKey& r) const;
    uint32_t Serialize(std::string& dst) const;
    int Deserialize(const std::string& src);

    const std::string& private_key() const {
        return private_key_;
    }

    const std::shared_ptr<BIGNUM>& bignum() const {
        return bignum_;
    }

private:

    std::shared_ptr<BIGNUM> bignum_;
    std::string private_key_;
};

}  // namespace security

}  // namespace zjchain
