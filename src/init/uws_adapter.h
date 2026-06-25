#pragma once

#include <App.h>
#include <string>
#include <map>
#include <sstream>
#include <nlohmann/json.hpp>

namespace shardora {
namespace init {

// Adapter to make uWebSockets compatible with httplib-style code
class UWSRequest {
public:
    UWSRequest(uWS::HttpRequest* req, const std::string& body_data) 
        : req_(req), body_(body_data) {
        ParseQueryParams();
    }

    // Construct from pre-copied query string (safe for async/onData callbacks
    // where the original HttpRequest* is no longer valid)
    UWSRequest(const std::string& query, const std::string& body_data)
        : req_(nullptr), query_copy_(query), body_(body_data) {
        ParseQueryParams();
    }

    std::string get_param_value(const std::string& key) const {
        auto it = params_.find(key);
        return it != params_.end() ? it->second : "";
    }

    bool has_param(const std::string& key) const {
        return params_.find(key) != params_.end();
    }

    std::string body;

private:
    void ParseQueryParams() {
        body = body_;
        // Parse URL query parameters
        std::string query;
        if (req_) {
            query = std::string(req_->getQuery());
        } else {
            query = query_copy_;
        }
        if (!query.empty()) {
            ParseParams(query);
        }
        
        // Parse POST body parameters (application/x-www-form-urlencoded)
        if (!body_.empty() && body_.find('=') != std::string::npos) {
            ParseParams(body_);
        }
    }

    void ParseParams(const std::string& str) {
        std::istringstream iss(str);
        std::string pair;
        while (std::getline(iss, pair, '&')) {
            auto pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = pair.substr(0, pos);
                std::string value = pair.substr(pos + 1);
                params_[key] = UrlDecode(value);
            }
        }
    }

    std::string UrlDecode(const std::string& str) {
        std::string result;
        result.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                int value;
                std::istringstream is(str.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    result += static_cast<char>(value);
                    i += 2;
                } else {
                    result += str[i];
                }
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }

    uWS::HttpRequest* req_;
    std::string query_copy_;
    std::string body_;
    std::map<std::string, std::string> params_;
};

class UWSResponse {
public:
    UWSResponse() : status_code_(200), content_type_("text/plain") {}

    void set_content(const std::string& content, const std::string& content_type) {
        content_ = content;
        content_type_ = content_type;
    }

    // Explicit overload for const char* to avoid ambiguity
    void set_content(const char* content, const char* content_type) {
        content_ = std::string(content);
        content_type_ = std::string(content_type);
    }

    void set_content(const nlohmann::json& json, const std::string& content_type) {
        content_ = json.dump();
        content_type_ = content_type;
    }

    const std::string& content() const { return content_; }
    const std::string& content_type() const { return content_type_; }
    int status_code() const { return status_code_; }

private:
    std::string content_;
    std::string content_type_;
    int status_code_;
};

} // namespace init
} // namespace shardora
