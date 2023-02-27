#pragma once

#include <memory>
#include <cassert>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>
#include <openssl/opensslv.h>

#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

struct Curve {
    Curve()
        : group_(EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_clear_free),
          order_(BN_new(), BN_clear_free) {
        assert(group_ != nullptr);
        assert(order_ != nullptr);
        int res = EC_GROUP_get_order(group_.get(), order_.get(), NULL);
        assert(res != 0);
    }

    ~Curve() {}

    std::shared_ptr<EC_GROUP> group_;
    std::shared_ptr<BIGNUM> order_;
};

}  // namespace security

}  // namespace zjchain
