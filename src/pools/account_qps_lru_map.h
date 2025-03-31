#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "common/hash.h"
#include "common/spin_mutex.h"
#include "common/utils.h"
#include "protos/prefix_db.h"
#include "protos/address.pb.h"

namespace shardora {

namespace pools {

using QpsWindow = std::map<uint64_t, uint32_t>;
using QpsWindowPtr = std::shared_ptr<QpsWindow>;
static const uint32_t kQpsWindowSeconds = 10lu;
static const uint32_t kQpsWindowMaxQps = 1024u * 2u;

template<uint32_t kBucketSize>
class AccountQpsLruMap {
public:
    ~AccountQpsLruMap() {}

    bool check(const std::string& addr) {
        if (item_map_.count(addr)) {
            item_list_.erase(item_map_[addr]);
            item_map_.erase(addr);
        }

        item_list_.push_front(addr);
        item_map_[addr] = item_list_.begin();
        QpsWindowPtr value = nullptr;
        auto now_timestamp_seconds_10 = common::TimeUtils::TimestampSeconds() / kQpsWindowSeconds;
        auto iter = qps_user_map_.find(addr);
        if (iter != qps_user_map_.end()) {
            value = iter->second;
            uint32_t now_qps_windows = 0;
            for (auto tm_iter = value->begin(); tm_iter != value->end();) {
                if (tm_iter->first + kQpsWindowSeconds * 2 >= now_timestamp_seconds_10) {
                    now_qps_windows += tm_iter->second;
                    ++tm_iter;
                } else {
                    tm_iter = value->erase(tm_iter);
                }
            }

            auto tm_iter = value->find(now_timestamp_seconds_10);
            if (tm_iter == value->end()) {
                (*value)[now_timestamp_seconds_10] = 0;
            }

            if (now_qps_windows >= kQpsWindowMaxQps) {
                return false;
            }
        } else {
            value = std::make_shared<QpsWindow>();
            (*value)[now_timestamp_seconds_10] = 0;
        }

        (*value)[now_timestamp_seconds_10] += 1;
        qps_user_map_[addr] = value;
        if (item_list_.size() > kBucketSize) {
            std::string& last = item_list_.back();
            auto iter = qps_user_map_.find(last);
            if (iter != qps_user_map_.end()) {
                qps_user_map_.erase(iter);
            }

            item_map_.erase(last);
            item_list_.pop_back();
        }

        return true;
    }

private:
    std::list<std::string> item_list_;
    std::unordered_map<std::string, typename std::list<std::string>::iterator> item_map_;
    std::unordered_map<std::string, QpsWindowPtr> qps_user_map_;

};

};  // namespace block

};  // namespace shardora
