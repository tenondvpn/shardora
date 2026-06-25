#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>

#include "common/thread_safe_queue.h"
#include "common/log.h"

namespace shardora {
namespace network {

// NeighborIpManager: lock-free id → public_ip mapping.
//
// Any thread that receives a signature-verified message calls Update(id, ip).
// Updates are enqueued into a ThreadSafeQueue and drained by a dedicated
// background thread into an unordered_map, so no mutex is needed on the
// read/write path.
//
// GetIp() is called from the background thread only (or after Stop()).
// For cross-thread reads, use GetIpThreadSafe() which flushes the queue first.
class NeighborIpManager {
public:
    static NeighborIpManager* Instance() {
        static NeighborIpManager ins;
        return &ins;
    }

    ~NeighborIpManager() { Stop(); }

    void Start() {
        running_.store(true);
    }

    void Stop() {
        running_.store(false);
        if (drain_thread_.joinable()) drain_thread_.join();
    }

    // Called from any thread after signature verification passes.
    void Update(const std::string& id, const std::string& public_ip) {
        if (id.empty() || public_ip.empty()) return;
        queue_.push({id, public_ip});
    }

    // Query from the drain thread (no lock needed).
    std::string GetIp(const std::string& id) const {
        auto it = map_.find(id);
        return (it != map_.end()) ? it->second : "";
    }

    // Query from any thread: flush pending updates first, then read.
    std::string GetIpThreadSafe(const std::string& id) {
        Flush();
        return GetIp(id);
    }

    size_t Size() const { return map_.size(); }

private:
    struct Entry {
        std::string id;
        std::string public_ip;
    };

    NeighborIpManager() = default;
    void Flush() {
        Entry e;
        while (queue_.pop(&e)) {
            map_[e.id] = e.public_ip;
            SHARDORA_DEBUG("[NeighborIpManager] updated id=%s ip=%s",
                       e.id.substr(0, 8).c_str(), e.public_ip.c_str());
        }
    }

    common::ThreadSafeQueue<Entry, 65536> queue_;
    std::unordered_map<std::string, std::string> map_;
    std::thread drain_thread_;
    std::atomic<bool> running_{false};

    DISALLOW_COPY_AND_ASSIGN(NeighborIpManager);
};

}  // namespace network
}  // namespace shardora
