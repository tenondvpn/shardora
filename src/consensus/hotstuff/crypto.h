#pragma once
#include <consensus/hotstuff/types.h>

namespace shardora {

namespace consensus {

class Crypto {
public:
    Crypto() = default;
    virtual ~Crypto() = 0;

    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;

    void Sign(const std::string&);
    bool Verify();
    void RecoverSign();
};

} // namespace consensus

} // namespace shardora


