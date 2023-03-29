#pragma once

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "common/random.h"
#include "common/hash.h"
#include "common/limit_heap.h"
#include "common/global_info.h"
#include "protos/vss.pb.h"
#include "vss/vss_utils.h"

namespace zjchain {

namespace vss {

class RandomNum {
public:
    explicit RandomNum(bool is_local = false) : is_local_(is_local) {}

    ~RandomNum() {}

    void ResetStatus() {
        std::lock_guard<std::mutex> guard(mutex_);
        Clear();
    }

    void SetId(const std::string& id) {
        owner_id_ = id;
    }

    std::string GetId() {
        return owner_id_;
    }

    void OnTimeBlock(uint64_t tm_block_tm) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (tm_block_tm_ >= tm_block_tm) {
            return;
        }

        Clear();
        if (!is_local_) {
            return;
        }

        srand(time(NULL));
        final_random_num_ ^= common::Random::RandomUint64();
        random_num_hash_ = common::Hash::Hash64(std::to_string(final_random_num_));
        tm_block_tm_ = tm_block_tm;
        valid_ = true;
        owner_id_ = common::GlobalInfo::Instance()->id();
    }

    void SetHash(const std::string& id, uint64_t hash_num) {
        std::lock_guard<std::mutex> guard(mutex_);
        // owner has came
        if (valid_ || is_local_ || !owner_id_.empty()) {
            return;
        }

        random_num_hash_ = hash_num;
        owner_id_ = id;
    }

    uint64_t GetHash() {
        std::lock_guard<std::mutex> guard(mutex_);
        return random_num_hash_;
    }

    uint64_t GetFinalRandomNum() {
        std::lock_guard<std::mutex> guard(mutex_);
        return final_random_num_;
    }
    
    void SetFinalRandomNum(const std::string& id, uint64_t final_random_num) {
        std::lock_guard<std::mutex> guard(mutex_);
        // random hash must coming
        if (valid_ || is_local_ || owner_id_ != id) {
            return;
        }

        auto rand_hash = common::Hash::Hash64(std::to_string(final_random_num));
        if (rand_hash == random_num_hash_) {
            final_random_num_ = final_random_num;
            valid_ = true;
        }
    }

    bool IsRandomValid() {
        return valid_;
    }

    bool IsRandomInvalid() {
        return invalid_;
    }

private:
    void Clear() {
        final_random_num_ = 0;
        tm_block_tm_ = 0;
        random_num_hash_ = 0;
        valid_ = false;
        owner_id_ = "";
    }

    std::mutex mutex_;
    uint64_t final_random_num_{ 0 };
    uint64_t tm_block_tm_{ 0 };
    uint64_t random_num_hash_{ 0 };
    bool valid_{ false };
    bool invalid_{ false };
    bool is_local_{ false };
    std::string owner_id_;

    DISALLOW_COPY_AND_ASSIGN(RandomNum);
};

typedef std::shared_ptr<RandomNum> RandomNumPtr;

}  // namespace vss

}  // namespace zjchain
