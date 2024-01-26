#pragma once

#include <map>
#include <vector>
#include <string>

#include "common/utils.h"

namespace zjchain {

namespace common {

enum KeyFlag { kInvalidKey = -1, kNoValue, kMaybeValue, kMustValue };
enum ParseErrorCode {
    kParseSuccess = 0,
    kParseFailed = 1,
};

class ParserArgs {
public:
    ParserArgs();
    ~ParserArgs();
    bool AddArgType(const char short_name, const char* long_name, KeyFlag flag);
    int Parse(const std::string& paras, std::string& err_pos);
    int Get(const std::string& key, int& value);
    int Get(const std::string& key, uint16_t& value);
    int Get(const std::string& key, std::string& value);
    bool Has(const std::string& key) const;

private:
    KeyFlag GetKeyFlag(std::string &key);
    void RemoveKeyFlag(std::string & paras);
    bool GetWord(std::string & params, std::string & word);
    bool IsDuplicateKey(const std::string &key);

    struct Option {
        std::string long_name;
        char short_name;
        KeyFlag flag;
    };

    std::vector<Option> args_;
    std::map<std::string, std::vector<std::string>> result_;

    DISALLOW_COPY_AND_ASSIGN(ParserArgs);
};

}  // namespace common

}  // namespace zjchain
