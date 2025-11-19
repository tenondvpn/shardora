#pragma once

#include <common/limit_hash_set.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "common/limit_hash_set.h"
#include "common/spin_mutex.h"
#include "common/thread_safe_queue.h"
#include "common/unique_set.h"
#include "common/unique_map.h"
#include "common/lru_set.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace transport {

class MessageHandler;
class MultiThreadHandler;

class ThreadHandler {
public:
    ThreadHandler(
        MultiThreadHandler* msg_handler,
        std::condition_variable& wait_con,
        std::mutex& wait_mutex);
    ~ThreadHandler();
    void Join();

private:
    void HandleMessage();

    std::shared_ptr<std::thread> thread_{ nullptr };
    bool destroy_{ false };
    MultiThreadHandler* msg_handler_ = nullptr;
    std::condition_variable& wait_con_;
    std::mutex& wait_mutex_;

    DISALLOW_COPY_AND_ASSIGN(ThreadHandler);
};

typedef std::shared_ptr<ThreadHandler> ThreadHandlerPtr;

class MultiThreadHandler {
public:
    MultiThreadHandler();
    ~MultiThreadHandler();
    int Init(std::shared_ptr<db::Db>& db, std::shared_ptr<security::Security>& security);
    void Start();
    void HandleMessage(MessagePtr& msg_ptr);
    MessagePtr GetMessageFromQueue(uint32_t thread_idx, bool);
    void Destroy();
    void NewHttpServer(MessagePtr& msg_ptr) {
        http_server_message_queue_.push(msg_ptr);
    }

    void AddFirewallCheckCallback(int32_t type, FirewallCheckCallback cb) {
        assert(type < common::kMaxMessageTypeCount);
        assert(firewall_checks_[type] == nullptr);
        firewall_checks_[type] = cb;
    }

    void ThreadWaitNotify() {
        std::unique_lock<std::mutex> lock(thread_wait_mutex_);
        thread_wait_con_.notify_one();
    }

    // void AddLocalBroadcastedMessages(uint64_t msg_hash) {
    //     auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    //     local_broadcast_messages_[thread_idx].push(msg_hash);
    // }

private:
    struct SavedBlockQueueItem {
        SavedBlockQueueItem(uint32_t c_pool, uint64_t c_height, uint32_t p, uint64_t h)
            : checked_pool(c_pool), checked_height(c_height), pool(p), height(h) {}
        uint32_t checked_pool;
        uint64_t checked_height;
        uint32_t pool;
        uint64_t height;
    };

    typedef std::shared_ptr<SavedBlockQueueItem> SavedBlockQueueItemPtr;
    void Join();
    int StartTcpServer();
    int32_t GetPriority(MessagePtr& msg_ptr);
    bool IsMessageUnique(uint64_t msg_hash);
    void InitThreadPriorityMessageQueues();
    uint8_t GetThreadIndex(MessagePtr& msg_ptr);
    void HandleSyncBftTimeout(MessagePtr& msg_ptr);
    void SaveKeyValue(const transport::protobuf::Header& msg, db::DbWriteBatch& db_batch);
    bool IsFromMessageUnique(const std::string& from_ip, uint64_t msg_hash);
    int CheckMessageValid(MessagePtr& msg_ptr);
    int CheckSignValid(MessagePtr& msg_ptr);
    int CheckDhtMessageValid(MessagePtr& msg_ptr);

    static const int kQueueObjectCount = 1024 * 1024;

    std::vector<ThreadHandlerPtr> thread_vec_;
    bool inited_{ false };
    common::LRUSet<uint64_t> unique_message_sets2_{ 102400 }; // 10M+ 左右，10000 tps 情况下能够忍受 10s 消息延迟
    common::ThreadSafeQueue<MessagePtr>** threads_message_queues_;
    common::ThreadSafeQueue<MessagePtr> http_server_message_queue_;
    common::ThreadSafeQueue<SavedBlockQueueItemPtr> saved_block_queue_;
    std::condition_variable* wait_con_ = nullptr;
    std::mutex* wait_mutex_ = nullptr;
    uint32_t consensus_thread_count_ = 4;
    uint32_t all_thread_count_ = 4;
    uint8_t robin_index_ = 0;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_set<uint64_t> committed_heights_[common::kInvalidPoolIndex];
    std::shared_ptr<security::Security> security_ = nullptr;
    FirewallCheckCallback firewall_checks_[common::kMaxMessageTypeCount] = { nullptr };
    common::LimitHashSet<uint64_t> from_unique_message_sets_{10240};
    std::condition_variable thread_wait_con_;
    std::mutex thread_wait_mutex_;
    // common::ThreadSafeQueue<uint64_t> local_broadcast_messages_[common::kMaxThreadCount];

#ifndef NDEBUG
    uint32_t msg_type_count_[common::kMaxMessageTypeCount] = {0};
    uint64_t prev_log_msg_type_tm_ = 0;
#endif

    DISALLOW_COPY_AND_ASSIGN(MultiThreadHandler);
};

}  // namespace transport

}  // namespace shardora
