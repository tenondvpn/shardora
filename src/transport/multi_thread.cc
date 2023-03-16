#include "transport/multi_thread.h"

#include <functional>

#include "common/utils.h"
#include "common/global_info.h"
#include "common/time_utils.h"
#include "protos/prefix_db.h"
#include "transport/processor.h"
#include "transport/transport_utils.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace transport {

ThreadHandler::ThreadHandler(
        uint32_t thread_idx,
        MultiThreadHandler* msg_handler,
        std::condition_variable& wait_con,
        std::mutex& wait_mutex)
        : thread_idx_(thread_idx),
        msg_handler_(msg_handler),
        wait_con_(wait_con),
        wait_mutex_(wait_mutex) {
    thread_.reset(new std::thread(&ThreadHandler::HandleMessage, this));
    thread_->detach();
}

ThreadHandler::~ThreadHandler() {}

void ThreadHandler::Join() {
    destroy_ = true;
    if (thread_) {
        thread_ = nullptr;
    }
}

void ThreadHandler::HandleMessage() {
    while (!destroy_) {
        while (!destroy_) {
            auto msg_ptr = msg_handler_->GetMessageFromQueue(thread_idx_);
            if (!msg_ptr) {
                break;
            }

            msg_ptr->header.set_hop_count(msg_ptr->header.hop_count() + 1);
            msg_ptr->thread_idx = thread_idx_;
            Processor::Instance()->HandleMessage(msg_ptr);
        }

        if (thread_idx_ + 1 < common::GlobalInfo::Instance()->message_handler_thread_count()) {
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->thread_idx = thread_idx_;
            msg_ptr->header.set_type(common::kConsensusTimerMessage);
            Processor::Instance()->HandleMessage(msg_ptr);
        }

        if (thread_idx_ + 1 == common::GlobalInfo::Instance()->message_handler_thread_count()) {
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->thread_idx = thread_idx_;
            msg_ptr->header.set_type(common::kPoolTimerMessage);
            Processor::Instance()->HandleMessage(msg_ptr);
        }

        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

MultiThreadHandler::MultiThreadHandler() {}

MultiThreadHandler::~MultiThreadHandler() {
    Destroy();
}

int MultiThreadHandler::Init(std::shared_ptr<db::Db>& db) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    all_thread_count_ = common::GlobalInfo::Instance()->message_handler_thread_count();
    consensus_thread_count_ = common::GlobalInfo::Instance()->message_handler_thread_count() - 1;
    TRANSPORT_INFO("MultiThreadHandler::Init() ...");
    if (inited_) {
        TRANSPORT_WARN("MultiThreadHandler::Init() before");
        return kTransportError;
    }

    InitThreadPriorityMessageQueues();
    if (StartTcpServer() != kTransportSuccess) {
        return kTransportError;
    }

    wait_con_ = new std::condition_variable[all_thread_count_];
    wait_mutex_ = new std::mutex[all_thread_count_];
    unique_message_sets_.Init(1024 * 10, 32);
    inited_ = true;
    TRANSPORT_INFO("MultiThreadHandler::Init() success");
    return kTransportSuccess;
}

void MultiThreadHandler::Start() {
    for (uint32_t i = 0; i < all_thread_count_; ++i) {
        thread_vec_.push_back(std::make_shared<ThreadHandler>(
            i, this, wait_con_[i], wait_mutex_[i]));
    }
}

int MultiThreadHandler::StartTcpServer() {
    return kTransportSuccess;
}

void MultiThreadHandler::Destroy() {
    if (!inited_) {
        return;
    }

    for (uint32_t i = 0; i < thread_vec_.size(); ++i) {
        thread_vec_[i]->Join();
    }
    thread_vec_.clear();
    inited_ = false;
}

int32_t MultiThreadHandler::GetPriority(int32_t msg_type) {
    switch (msg_type) {
    case common::kConsensusMessage:
        return kTransportPriorityHighest;
    case common::kElectMessage:
    case common::kVssMessage:
        return kTransportPriorityHigh;
    default:
        return kTransportPriorityLowest;
    }
}

void MultiThreadHandler::HandleMessage(MessagePtr& msg_ptr) {
    uint32_t priority = GetPriority(msg_ptr->header.type());
    if (thread_vec_.empty()) {
        return;
    }

    if (msg_ptr->header.hop_count() >= kMaxHops) {
        return;
    }

    if (!IsMessageUnique(msg_ptr->header.hash64())) {
        return;
    }

    auto queue_idx = GetThreadIndex(msg_ptr);
    threads_message_queues_[queue_idx][priority].push(msg_ptr);
    wait_con_[queue_idx % all_thread_count_].notify_one();
}

uint8_t MultiThreadHandler::GetThreadIndex(MessagePtr& msg_ptr) {
    switch (msg_ptr->header.type()) {
    case common::kDhtMessage:
    case common::kNetworkMessage:
    case common::kSyncMessage:
    case common::kElectMessage:
    case common::kVssMessage:
    case common::kBlsMessage:
    case common::kPoolsMessage:
        return consensus_thread_count_;
    case common::kConsensusMessage:
        return msg_ptr->header.zbft().pool_index() % consensus_thread_count_;
    default:
        return consensus_thread_count_;
    }
}

bool MultiThreadHandler::IsMessageUnique(uint64_t msg_hash) {
    return unique_message_sets_.add(msg_hash);
}
 
MessagePtr MultiThreadHandler::GetMessageFromQueue(uint32_t thread_idx) {
    for (uint32_t i = 0; i < all_thread_count_; ++i) {
        if (i % all_thread_count_ == thread_idx) {
            for (uint32_t j = kTransportPrioritySystem; j < kTransportPriorityMaxCount; ++j) {
                if (threads_message_queues_[i][j].size() > 0) {
                    MessagePtr msg_obj;
                    threads_message_queues_[i][j].pop(&msg_obj);
                    return msg_obj;
                }
            }
        }
    }

    // handle http/ws request
    if (thread_idx == consensus_thread_count_) {
        if (http_server_message_queue_.size() > 0) {
            MessagePtr msg_obj;
            http_server_message_queue_.pop(&msg_obj);
            return msg_obj;
        }
    }
    
    return nullptr;
}

void MultiThreadHandler::Join() {
    if (!inited_) {
        return;
    }

    for (uint32_t i = 0; i < thread_vec_.size(); ++i) {
        thread_vec_[i]->Join();
    }
    thread_vec_.clear();
    inited_ = false;
    for (uint32_t i = 0; i < all_thread_count_; ++i) {
        delete[] threads_message_queues_[i];
    }

    delete[] threads_message_queues_;
    delete[] wait_con_;
    delete[] wait_mutex_;
}

void MultiThreadHandler::InitThreadPriorityMessageQueues() {
    threads_message_queues_ = new common::ThreadSafeQueue<MessagePtr>*[all_thread_count_];
    for (uint32_t i = 0; i < all_thread_count_; ++i) {
        threads_message_queues_[i] =
            new common::ThreadSafeQueue<MessagePtr>[kTransportPriorityMaxCount];
    }
}

}  // namespace transport

}  // namespace zjchain
