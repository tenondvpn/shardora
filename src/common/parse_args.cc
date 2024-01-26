#include "common/parse_args.h"

#include <list>

#include "common/string_utils.h"

namespace zjchain {

namespace common {

ParserArgs::ParserArgs() {}

ParserArgs::~ParserArgs() {}

int ParserArgs::Get(const std::string& key, int& value) {
    auto iter = result_.find(key);
    if (iter == result_.end()) {
        return kParseFailed;
    }

    if (iter->second.empty()) {
        return kParseFailed;
    }

    if (!StringUtil::ToInt32(iter->second[0], &value)) {
        return kParseFailed;
    }

    return kParseSuccess;
}

int ParserArgs::Get(const std::string& key, uint16_t& value) {
    int tmp_value = 0;
    if (Get(key, tmp_value) != 0) {
        return kParseFailed;
    }

    value = static_cast<uint16_t>(tmp_value);
    return kParseSuccess;
}

int ParserArgs::Get(const std::string& key, std::string& value) {
    auto iter = result_.find(key);
    if (iter == result_.end()) {
        return kParseFailed;
    }

    if (iter->second.empty()) {
        return kParseFailed;
    }

    value = iter->second[0];
    return kParseSuccess;
}

bool ParserArgs::Has(const std::string& key) const {
    auto iter = result_.find(key);
    if (iter == result_.end()) {
        return false;
    }

    return true;
}

bool ParserArgs::AddArgType(char short_name, const char * long_name, KeyFlag flag) {
    if (NULL == long_name && 0 == short_name) {
        return false;
    }
    Option tmp;
    tmp.long_name = long_name;
    tmp.short_name = short_name;
    tmp.flag = flag;
    args_.push_back(tmp);
    return true;
}

KeyFlag ParserArgs::GetKeyFlag(std::string &key) {
    for (uint32_t i = 0; i < args_.size(); ++i) {
        std::string short_name = "-";
        std::string long_name = "--";
        short_name += args_[i].short_name;
        long_name += args_[i].long_name;
        if (0 == key.compare(short_name) || (0 == key.compare(long_name))) {
            RemoveKeyFlag(key);
            return args_[i].flag;
        }
    }
    return kInvalidKey;
}

void ParserArgs::RemoveKeyFlag(std::string& word) {
    if (word.size() >= 2) {
        if (word[1] == '-') {
            word.erase(1, 1);
        }
        if (word[0] == '-') {
            word.erase(0, 1);
        }
    }
}

bool ParserArgs::GetWord(std::string& params, std::string& word) {
    size_t not_space_pos = params.find_first_not_of(' ', 0);
    if (not_space_pos == std::string::npos) {
        params.clear();
        word.clear();
        return true;
    }

    int length = params.size();
    std::list<char> special_char;
    for (int i = not_space_pos; i < length; i++) {
        char cur = params[i];
        bool is_ok = false;
        switch (cur) {
            case ' ': {
                if (special_char.empty()) {
                    if (i != (length - 1)) {
                        params = std::string(params, i + 1, length - i - 1);
                    } else {
                        params.clear();
                    }
                    is_ok = true;
                } else {
                    if (special_char.back() == '\\') {
                        special_char.pop_back();
                    }
                    word.append(1, cur);
                }
                break;
            }
            case '"': {
                if (special_char.empty()) {
                    special_char.push_back(cur);
                } else if (special_char.back() == cur) {
                    special_char.pop_back();
                } else if (special_char.back() == '\\') {
                    word.append(1, cur);
                    special_char.pop_back();
                } else {
                    word.clear();
                    return false;
                }
                break;
            }
            case '\\': {
                if (special_char.empty()) {
                    special_char.push_back(cur);
                } else if (special_char.back() == '"') {
                    if (i < (length - 1)) {
                        if ('"' == params[i + 1] || '\\' == params[i + 1]) {
                            special_char.push_back(cur);
                        } else {
                            word.append(1, cur);
                        }
                    } else {
                        word.clear();
                        return false;
                    }
                } else if ('\\' == special_char.back()) {
                    word.append(1, cur);
                    special_char.pop_back();
                } else {
                    word.clear();
                    return false;
                }
                break;
            }
            default: {
                word.append(1, params[i]);
                if (i == (length - 1)) {
                    is_ok = true;
                    params.clear();
                }
                break;
            }
        }
        if (is_ok) {
            return true;
        }
    }

    if (special_char.empty()) {
        params.clear();
        return true;
    }
    return false;
}

bool ParserArgs::IsDuplicateKey(const std::string& key) {
    if (result_.find(key) != result_.end()) {
        return true;
    }

    for (uint32_t i = 0; i < args_.size(); ++i) {
        if ((key.compare(args_[i].long_name) == 0 &&
                result_.find(std::string(1, args_[i].short_name)) != result_.end()) ||
                (key.compare(std::string(1, args_[i].short_name)) == 0 &&
                result_.find(args_[i].long_name) != result_.end())) {
            return true;
        }
    }
    return false;
}

int ParserArgs::Parse(const std::string& params, std::string& err_pos) {
    std::string tmp_string = params;
    KeyFlag key_flag = kInvalidKey;
    std::string sKey = "";
    bool finded_value = false;
    while (!tmp_string.empty()) {
        std::string word = "";
        bool ret = GetWord(tmp_string, word);
        if (ret == false) {
            err_pos = tmp_string;
            return kParseFailed;
        } else {
            KeyFlag tmp_flag = GetKeyFlag(word);
            if (IsDuplicateKey(word)) {
                err_pos = tmp_string;
                return kParseFailed;
            }
            if (tmp_flag != kInvalidKey) {
                if (tmp_flag == kMustValue && key_flag == kMustValue && !finded_value) {
                    err_pos = tmp_string;
                    return kParseFailed;
                }
                key_flag = tmp_flag;
                std::vector<std::string> tmp;
                result_[word] = tmp;
                sKey = word;
                finded_value = false;
            } else {
                switch (key_flag) {
                case kMaybeValue:
                case kMustValue: {
                    auto it = result_.find(sKey);
                    if (it != result_.end()) {
                        it->second.push_back(word);
                        finded_value = true;
                    } else {
                        err_pos = tmp_string;
                        return kParseFailed;
                    }
                    break;
                }
                case kNoValue:
                    err_pos = tmp_string;
                    return kParseFailed;
                default:
                    err_pos = tmp_string;
                    return kParseFailed;
                }
            }
        }
    }
    return kParseSuccess;
}

}  // namespace common

}  // namespace zjchain
