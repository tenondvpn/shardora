#pragma once

#include <map>

namespace zjchain {

namespace common {

class Config {
public:
    Config();
    ~Config();

    Config(const Config& other) {
        config_map_ = other.config_map_;
    }

    Config operator=(const Config& other) {
        if (this != &other) {
            config_map_ = other.config_map_;
        }
        return *this;
    }

    bool Init(const std::string& conf);
    bool InitWithContent(const std::string& content);
    bool DumpConfig(const std::string& conf);
    bool Get(const std::string& field, const std::string& key, std::string& value) const;
    bool Get(const std::string& field, const std::string& key, bool& value) const;
    bool Get(const std::string& field, const std::string& key, int8_t& value) const;
    bool Get(const std::string& field, const std::string& key, uint8_t& value) const;
    bool Get(const std::string& field, const std::string& key, int16_t& value) const;
    bool Get(const std::string& field, const std::string& key, uint16_t& value) const;
    bool Get(const std::string& field, const std::string& key, int32_t& value) const;
    bool Get(const std::string& field, const std::string& key, uint32_t& value) const;
    bool Get(const std::string& field, const std::string& key, int64_t& value) const;
    bool Get(const std::string& field, const std::string& key, uint64_t& value) const;
    bool Get(const std::string& field, const std::string& key, float& value) const;
    bool Get(const std::string& field, const std::string& key, double& value) const;

    bool Set(const std::string& field, const std::string& key, const std::string& value);
    bool Set(const std::string& field, const std::string& key, int16_t value);
    bool Set(const std::string& field, const std::string& key, uint16_t value);
    bool Set(const std::string& field, const std::string& key, bool value);
    bool Set(const std::string& field, const std::string& key, int32_t value);
    bool Set(const std::string& field, const std::string& key, uint32_t value);
    bool Set(const std::string& field, const std::string& key, int64_t value);
    bool Set(const std::string& field, const std::string& key, uint64_t value);
    bool Set(const std::string& field, const std::string& key, float value);
    bool Set(const std::string& field, const std::string& key, double value);

private:
    bool AddField(const std::string& field);
    bool AddKey(const std::string& field, const std::string& key, const std::string& value);
    bool HandleFiled(const std::string& field, std::string& field_val);
    bool HandleKeyValue(const std::string& filed, const std::string& key_value);

    std::map<std::string, std::map<std::string, std::string>> config_map_;
};

}  // namespace common

}  // namespace zjchain
