#include "common/config.h"

#include <iostream>
#include <string>

#include "common/log.h"
#include "common/string_utils.h"
#include "common/split.h"
#include "common/encode.h"

//#define ENCODE_CONFIG_CONTENT

namespace zjchain {

namespace common {

static const uint32_t kConfigMaxLen = 1024 * 1024;
static const std::string kConfigEncKey = "dfger45eD4fe$^&Idfger45eD4fe$^&I";

Config::Config() {}
Config::~Config() {}

bool Config::Get(const std::string& field, const std::string& key, std::string& value)  const {
    auto iter = config_map_.find(field);
    if (iter == config_map_.end()) {
        ZJC_ERROR("invalid field[%s]", field.c_str());
        return false;
    }

    auto kv_iter = iter->second.find(key);
    if (kv_iter == iter->second.end()) {
        ZJC_ERROR("invalid field[%s] key[%s]", field.c_str(), key.c_str());
        return false;
    }

    value = kv_iter->second;
    return true;
}

bool Config::Get(const std::string& field, const std::string& key, bool& value)  const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    if (tmp_val == "1") {
        value = true;
        return true;
    }

    if (tmp_val == "true") {
        value = true;
        return true;
    }

    if (tmp_val == "0") {
        value = false;
        return true;
    }

    if (tmp_val == "false") {
        value = false;
        return true;
    }
    return false;
}

bool Config::Get(const std::string& field, const std::string& key, int8_t& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToInt8(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, uint8_t& value) const {
        std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToUint8(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, int16_t& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToInt16(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, uint16_t& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToUint16(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, int32_t& value)  const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToInt32(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, uint32_t& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToUint32(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, int64_t& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToInt64(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, uint64_t& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToUint64(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, float& value)  const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToFloat(tmp_val, &value);
}

bool Config::Get(const std::string& field, const std::string& key, double& value) const {
    std::string tmp_val;
    if (!Get(field, key, tmp_val)) {
        return false;
    }

    return StringUtil::ToDouble(tmp_val, &value);
}

bool Config::Set(const std::string& field, const std::string& key, const std::string& value) {
    auto iter = config_map_.find(field);
    if (iter == config_map_.end()) {
        auto ins_iter = config_map_.insert(std::make_pair(
                field, std::map<std::string, std::string>()));
        if (!ins_iter.second) {
            return false;
        }

        iter = config_map_.find(field);
    }

    iter->second[key] = value;
    return true;
}

bool Config::Set(const std::string& field, const std::string& key, bool value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, int16_t value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, uint16_t value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, int32_t value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, uint32_t value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, int64_t value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, uint64_t value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, float value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::Set(const std::string& field, const std::string& key, double value) {
    std::string val = std::to_string(value);
    return Set(field, key, val);
}

bool Config::DumpConfig(const std::string& conf) {
    FILE* fd = fopen(conf.c_str(), "w");
    if (fd == NULL) {
        ZJC_ERROR("open config file[%s] failed!", conf.c_str());
        return false;
    }

    bool res = true;
    for (auto iter = config_map_.begin(); iter != config_map_.end(); ++iter) {
        std::string filed = std::string("[") + iter->first + "]\n";
        size_t ws = fwrite(filed.c_str(), 1, filed.size(), fd);
        if (ws != filed.size()) {
            ZJC_ERROR("write file failed!");
            res = false;
            break;
        }

        for (auto key_iter = iter->second.begin(); key_iter != iter->second.end(); ++key_iter) {
            std::string kv = key_iter->first + "=" + key_iter->second + "\n";
            size_t tm_ws = fwrite(kv.c_str(), 1, kv.size(), fd);
            if (tm_ws != kv.size()) {
                ZJC_ERROR("write file failed!");
                res = false;
                break;
            }
        }

        if (!res) {
            break;
        }
    }
    fclose(fd);
    return res;
}

bool Config::InitWithContent(const std::string& content) {
    common::Split<> spliter(content.c_str(), '\n', content.size());
    bool res = true;
    std::string filed;
    for (uint32_t i = 0; i < spliter.Count(); ++i) {
        std::string line(spliter[i], spliter.SubLen(i));
        if (line.size() >= kConfigMaxLen) {
            ZJC_ERROR("line size exceeded %d", kConfigMaxLen);
            printf("line size exceeded %d\n", kConfigMaxLen);
            res = false;
            break;
        }

        if (line[0] == '#') {
            continue;
        }

        if (line.find(']') != std::string::npos) {
            if (!HandleFiled(line, filed)) {
                ZJC_ERROR("handle field failed[%s][%s]", line.c_str(), filed.c_str());
                res = false;
                break;
            }
            continue;
        }

        if (line.find('=') != std::string::npos) {
            if (!HandleKeyValue(filed, line)) {
                ZJC_ERROR("handle key value failed[%s]", line.c_str());
                printf("handle key value failed[%s]\n", line.c_str());
                res = false;
                break;
            }
            continue;
        }

        for (uint32_t j = 0; j < line.size(); ++j) {
            if (line[j] != ' ' && line[j] != ' ' && line[j] != '\n') {
                ZJC_ERROR("line illegal[%s]", line.c_str());
                printf("line illegal[%s]\n", line.c_str());
                res = false;
                break;
            }
        }

        if (!res) {
            break;
        }
    }

    return res;
}

bool Config::Init(const std::string& conf) {
    FILE* fd = fopen(conf.c_str(), "r");
    if (fd == NULL) {
        ZJC_ERROR("open config file[%s] failed!", conf.c_str());
        printf("open config file[%s] failed!\n", conf.c_str());
        return false;
    }
#ifdef ENCODE_CONFIG_CONTENT
    fseek(fd, 0, SEEK_END);
    auto file_size = ftell(fd);
    if (file_size > 1024 * 1024) {
        ZJC_ERROR("read config file[%s] failed!", conf.c_str());
        printf("read config file[%s] failed! size error.[%ld]\n",
                conf.c_str(), file_size);
        return false;
    }

    fseek(fd, 0, SEEK_SET);

    char* buffer = new char[file_size];
    auto result = fread(buffer, 1, file_size, fd);
    if (result != file_size) {
        delete[]buffer;
        fclose(fd);
        ZJC_ERROR("read config file[%s] failed!", conf.c_str());
        printf("read config file[%s] failed! size error.[%ld][%ld]\n",
                conf.c_str(), result, file_size);
        return false;
    }

    std::string content(buffer, file_size);
    std::string dec_code_con = common::Encode::HexDecode(content);
    char* out = new char[dec_code_con.size()];
    int dec_res = security::Aes::Decrypt(
            (char*)dec_code_con.c_str(),
            dec_code_con.size(),
            (char*)kConfigEncKey.c_str(),
            kConfigEncKey.size(),
            out);
    std::string tmp_content(out, dec_code_con.size());
    delete[]out;
    delete[]buffer;
    fclose(fd);
    if (dec_res != security::kSecuritySuccess) {
        return false;
    }
    return InitWithContent(tmp_content);
#endif

    bool res = true;
    std::string filed;
    char* read_buf = new char[kConfigMaxLen];
    while (true) {
        char* read_res = fgets(read_buf, kConfigMaxLen, fd);
        if (read_res == NULL) {
            break;
        }

        std::string line(read_buf);
        if (line.size() >= kConfigMaxLen) {
            ZJC_ERROR("line size exceeded %d", kConfigMaxLen);
            printf("open config file[%s] failed!\n", conf.c_str());
            res = false;
            break;
        }

        if (line[0] == '#') {
            continue;
        }

        if (line.find(']') != std::string::npos) {
            if (!HandleFiled(line, filed)) {
                ZJC_ERROR("handle field failed[%s][%d]", line.c_str(), line.find(']'));
                res = false;
                break;
            }
            continue;
        }

        if (line.find('=') != std::string::npos) {
            if (!HandleKeyValue(filed, line)) {
                ZJC_ERROR("handle key value failed[%s]", line.c_str());
                res = false;
                break;
            }
            continue;
        }

        for (uint32_t i = 0; i < line.size(); ++i) {
            if (line[i] != ' ' && line[i] != ' ' && line[i] != '\n') {
                ZJC_ERROR("line illegal[%s]", line.c_str());
                res = false;
                break;
            }
        }

        if (!res) {
            break;
        }
    }

    fclose(fd);
    delete []read_buf;
    return res;
}

bool Config::HandleKeyValue(const std::string& filed, const std::string& key_value) {
    size_t eq_pos = key_value.find('=');
    int key_start_pos = -1;
    std::string key("");
    for (size_t i = 0; i < eq_pos; ++i) {
        if (key_value[i] == '#') {
            break;
        }

        if (key_value[i] == '=' || key_value[i] == '+' || key_value[i] == '-' ||
                key_value[i] == '*' || key_value[i] == '/') {
            ZJC_ERROR("invalid char[%c]", key_value[i]);
            printf("invalid char[%c]\n", key_value[i]);
            return false;
        }

        if (key_value[i] == ' ' || key_value[i] == '\t') {
            if (key_start_pos == -1) {
                continue;
            }

            for (size_t j = i; j < eq_pos; ++j) {
                if (key_value[j] != ' ' && key_value[j] != '\t' && key_value[j] != '\n') {
                    ZJC_ERROR("invalid char[ ][\\t][\\n]");
                    printf("invalid char[ ][\\t][\\n]\n");
                    return false;
                }
            }

            key = std::string(key_value.begin() + key_start_pos, key_value.begin() +  i);
            break;
        }

        if (key_start_pos == -1) {
            key_start_pos = i;
        }
    }
    if (key_start_pos == -1 || static_cast<int>(eq_pos) <= key_start_pos) {
        ZJC_ERROR("invalid key_start_pos[%d]", key_start_pos);
        printf("invalid key_start_pos[%d]\n", key_start_pos);
        return false;
    }
    if (key.empty()) {
        key = std::string(key_value.begin() + key_start_pos, key_value.begin() + eq_pos);
    }
    if (key.empty()) {
        ZJC_ERROR("invalid key_start_pos[%d]", key_start_pos);
        printf("invalid key_start_pos[%d]\n", key_start_pos);
        return false;
    }

    int value_start_pos = eq_pos + 1;
    std::string value("");
    for (size_t i = eq_pos + 1; i < key_value.size(); ++i) {
        if (key_value[i] == '#') {
            if (i > static_cast<size_t>(value_start_pos)) {
                value = std::string(key_value.begin() + value_start_pos, key_value.begin() + i);
            }
            break;
        }

        if (key_value[i] == '=') {
            ZJC_ERROR("invalid char[%c]", key_value[i]);
            printf("invalid char[%c]\n", key_value[i]);
            return false;
        }

        if (key_value[i] == ' ' || key_value[i] == '"') {
            continue;
        }

        if (key_value[i] == '\n') {
            if (value_start_pos == -1) {
                continue;
            }

            for (size_t j = i; j < key_value.size(); ++j) {
                if (key_value[j] == '#') {
                    break;
                }

                if (key_value[j] != ' ' && key_value[j] != '\t' && key_value[j] != '\n') {
                    ZJC_ERROR("invalid char[ ][\\t][\\n]");
                    printf("invalid char[ ][\\t][\\n]\n");
                    return false;
                }
            }

            value = std::string(key_value.begin() + value_start_pos, key_value.begin() + i);
            break;
        }

        if (value_start_pos == -1) {
            value_start_pos = i;
        }
    }
    if (value_start_pos == -1 || static_cast<int>(key_value.size()) <= value_start_pos) {
        ZJC_ERROR("invalid value_start_pos[%d]", value_start_pos);
        printf("invalid value_start_pos[%d]\n", value_start_pos);
        return false;
    }
    if (value.empty()) {
#ifdef ENCODE_CONFIG_CONTENT
        value = std::string(key_value.begin() + value_start_pos, key_value.end());
#else
        value = std::string(key_value.begin() + value_start_pos, key_value.end() - 1);
#endif
    }
    StringUtil::Trim(value);
    return AddKey(filed, key, value);
}

bool Config::HandleFiled(const std::string& field, std::string& field_val) {
    int start_pos = -1;
    for (uint32_t i = 0; i < field.size(); ++i) {
        if (field[i] == '#') {
            break;
        }

        if (field[i] == '=' || field[i] == '+' || field[i] == '-' ||
                field[i] == '*' || field[i] == '/') {
            ZJC_ERROR("unvalid char[%c]", field[i]);
            return false;
        }

        if (field[i] == '[') {
            start_pos = i + 1;
            continue;
        }

        if (field[i] == ']') {
            for (uint32_t j = i + 1; j < field.size(); ++j) {
                if (field[j] == '#') {
                    break;
                }

                if (field[j] != ' ' && field[j] != '\t' && field[j] != '\n') {
                    ZJC_ERROR("unvalid char[ ][\\n]");
                    return false;
                }
            }

            std::string str_filed(field.begin() + start_pos, field.begin() + i);
            AddField(str_filed);
            field_val = str_filed;
            return true;
        }

        if (field[i] == ' ') {
            if (start_pos == -1) {
                continue;
            }

            ZJC_ERROR("unvalid char[ ][\\n]");
            return false;
        }

        if (start_pos == -1) {
            ZJC_ERROR("unvalid start_pos");
            return false;
        }
    }
    return false;
}

bool Config::AddField(const std::string& field) {
    auto iter = config_map_.find(field);
    if (iter != config_map_.end()) {
        return false;
    }

    auto ins_iter = config_map_.insert(std::make_pair(
            field,
            std::map<std::string, std::string>()));
    return ins_iter.second;
}

bool Config::AddKey(const std::string& field, const std::string& key, const std::string& value) {
    auto iter = config_map_.find(field);
    if (iter == config_map_.end()) {
        printf("add key error, field not exists.[%s]\n", field.c_str());
        return false;
    }
    auto ins_iter = iter->second.insert(std::make_pair(key, value));
    return ins_iter.second;
}

}  // namespace common

}  // namespace zjchain
