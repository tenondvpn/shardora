#pragma once

#include <thread>
#include <unordered_map>

#include "common/thread_safe_queue.h"
#include "common/utils.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace transport {

class SyncBroadcast {
public:
    SyncBroadcast() {}
    SyncBroadcast(uint32_t bit_count, uint32_t hash_count);
    SyncBroadcast(const std::vector<uint64_t>& data, uint32_t hash_count);
    ~SyncBroadcast();

    void AddMessage(MessagePtr& msg_ptr) {
        msg_queue_.push(msg_ptr);
    }
    
private:
    void Run();
    void Add(uint64_t hash);
    void Remomve(uint64_t hash);
    bool Contain(uint64_t hash) const;
    uint32_t DiffCount(const SyncBroadcast& other);
    SyncBroadcast& operator=(const SyncBroadcast& src);
    bool operator==(const SyncBroadcast& r) const;
    bool operator!=(const SyncBroadcast& r) const;

    const std::vector<uint64_t>& data() const {
        assert(data_.size() <= 256);
        return data_;
    }

    uint32_t hash_count() {
        return hash_count_;
    }

    std::string Serialize() const;
    void Deserialize(const uint64_t* data, uint32_t count, uint32_t hash_count);

    std::vector<uint64_t> data_;
    uint32_t hash_count_{ 0 };
    common::ThreadSafeQueue<MessagePtr> msg_queue_;
    std::shared_ptr<std::thread> thread_ = nullptr;
    std::unordered_map<uint32_t, int32_t> index_with_count_;

};

}  // namespace transport

}  // namespace zjchain
