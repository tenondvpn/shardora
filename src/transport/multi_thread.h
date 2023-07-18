#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#include <parallel_hashmap/phmap.h>

#include "common/spin_mutex.h"
#include "common/thread_safe_queue.h"
#include "common/unique_set.h"
#include "common/unique_map.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace transport {

class MessageHandler;
class MultiThreadHandler;

class ThreadHandler {
public:
    ThreadHandler(
        uint32_t thread_idx,
        MultiThreadHandler* msg_handler,
        std::condition_variable& wait_con,
        std::mutex& wait_mutex);
    ~ThreadHandler();
    void Join();

private:
    void HandleMessage();

    std::shared_ptr<std::thread> thread_{ nullptr };
    bool destroy_{ false };
    uint32_t thread_idx_{ 0 };
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
    int Init(std::shared_ptr<db::Db>& db);
    void Start();
    void HandleMessage(MessagePtr& msg_ptr);
    MessagePtr GetMessageFromQueue(uint32_t thread_idx);
    void Destroy();
    void NewHttpServer(MessagePtr& msg_ptr) {
        http_server_message_queue_.push(msg_ptr);
    }

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
    int32_t GetPriority(int32_t msg_type);
    bool IsMessageUnique(uint64_t msg_hash);
    void InitThreadPriorityMessageQueues();
    uint8_t GetThreadIndex(MessagePtr& msg_ptr);
    void HandleSyncBlockResponse(MessagePtr& msg_ptr);
    void SaveKeyValue(const transport::protobuf::Header& msg, db::DbWriteBatch& db_batch);
    void CreateConsensusBlockMessage(
        std::shared_ptr<transport::TransportMessage>& new_msg_ptr,
        std::shared_ptr<block::protobuf::Block>& block_ptr);
    uint32_t GetMessagePriority(MessagePtr& msg_ptr);

    static const int kQueueObjectCount = 1024 * 1024;

    std::queue<std::shared_ptr<protobuf::Header>> local_queue_;
    std::vector<ThreadHandlerPtr> thread_vec_;
    bool inited_{ false };
    common::UniqueSet<uint64_t, 10240, 64> unique_message_sets_;
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
    std::unordered_map<uint64_t, std::shared_ptr<block::protobuf::Block>> waiting_check_block_map_[common::kInvalidPoolIndex];
    std::unordered_set<uint64_t> committed_heights_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(MultiThreadHandler);
};

}  // namespace transport

}  // namespace zjchain
