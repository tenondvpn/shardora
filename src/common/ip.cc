#include "common/ip.h"

#include "common/global_info.h"
#include "common/string_utils.h"

namespace zjchain {

namespace common {

Ip* Ip::Instance() {
    static Ip ins;
    return &ins;
}

Ip::Ip() {
    db_ = std::make_shared<GeoLite2PP::DB>(common::GlobalInfo::Instance()->ip_db_path());
    ZJC_INFO("MMDB version: %s, GeoLite2PP ver: %s",
        db_->get_lib_version_mmdb().c_str(),
        db_->get_lib_version_geolite2pp().c_str());
}

Ip::~Ip() {

}

std::string Ip::GetIpCountryIsoCode(const std::string& ip) {
    const auto m = db_->get_all_fields(ip);
    auto iter = m.find("country_iso_code");
    if (iter == m.end()) {
        return "";
    }

    return iter->second;
}

int Ip::GetIpLocation(const std::string& ip, float* latitude, float* longitude) {
    const auto m = db_->get_all_fields(ip);
    auto latitude_iter = m.find("latitude");
    if (latitude_iter == m.end()) {
        return 1;
    }

    auto longitude_iter = m.find("longitude");
    if (longitude_iter == m.end()) {
        return 1;
    }

    if (!common::StringUtil::ToFloat(latitude_iter->second, latitude)) {
        return 1;
    }

    if (!common::StringUtil::ToFloat(longitude_iter->second, longitude)) {
        return 1;
    }

    return 0;
}

std::string Ip::GetIpCountry(const std::string& ip) {
    const auto m = db_->get_all_fields(ip);
    auto iter = m.find("country");
    if (iter == m.end()) {
        return "";
    }

    return iter->second;
}

}  // namespace common

}  // namespace zjchain