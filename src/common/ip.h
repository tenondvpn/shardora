#pragma once

#include <memory>

#include <geoip/GeoLite2PP.hpp>

#include "common/utils.h"

namespace zjchain {

namespace common {

class Ip {
public:
    static Ip* Instance();
    std::string GetIpCountryIsoCode(const std::string& ip);
    int GetIpLocation(const std::string& ip, float* latitude, float* longitude);
    std::string GetIpCountry(const std::string& ip);

private:
    Ip();
    ~Ip();
    std::shared_ptr<GeoLite2PP::DB> db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Ip);
};

}  // namespace common

}  // namespace zjchain