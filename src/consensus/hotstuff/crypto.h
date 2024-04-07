#pragma once

namespace shardora {

namespace consensus {

class Crypto {
public:
    Crypto() = default;
    virtual ~Crypto() = 0;

    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;

    virtual void Sign() = 0;
    virtual bool Verify() = 0;
    virtual void RecoverSign() = 0;
};

} // namespace consensus

} // namespace shardora


