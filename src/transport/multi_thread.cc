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

        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        msg_ptr->thread_idx = thread_idx_;
        msg_ptr->header.set_type(common::kConsensusTimerMessage);
        Processor::Instance()->HandleMessage(msg_ptr);
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

MultiThreadHandler::MultiThreadHandler() {}

MultiThreadHandler::~MultiThreadHandler() {
    Destroy();
}

int MultiThreadHandler::Init(
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<db::Db>& db) {
    security_ptr_ = security_ptr;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    TRANSPORT_INFO("MultiThreadHandler::Init() ...");
    if (inited_) {
        TRANSPORT_WARN("MultiThreadHandler::Init() before");
        return kTransportError;
    }

    InitThreadPriorityMessageQueues();
    if (StartTcpServer() != kTransportSuccess) {
        return kTransportError;
    }

    thread_count_ = common::GlobalInfo::Instance()->message_handler_thread_count();
    wait_con_ = new std::condition_variable[thread_count_];
    wait_mutex_ = new std::mutex[thread_count_];
    for (uint32_t i = 0; i < thread_count_; ++i) {
        thread_vec_.push_back(std::make_shared<ThreadHandler>(
            i, this, wait_con_[i], wait_mutex_[i]));
    }

    unique_message_sets_.Init(1024 * 1, 32);
    inited_ = true;
    TRANSPORT_INFO("MultiThreadHandler::Init() success");
    return kTransportSuccess;
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
    case common::kBftMessage:
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
    wait_con_[queue_idx % thread_count_].notify_one();
}

uint8_t MultiThreadHandler::GetTxThreadIndex(MessagePtr& msg_ptr) {
    auto address_info = GetAddressInfo(
        security_ptr_->GetAddress(msg_ptr->header.tx_proto().pubkey()));
    if (address_info == nullptr ||
            address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        return 255;
    }

    msg_ptr->address_info = address_info;
    return address_info->pool_index() % thread_count_;
}

uint8_t MultiThreadHandler::GetThreadIndex(MessagePtr& msg_ptr) {
    switch (msg_ptr->header.type()) {
    case common::kDhtMessage:
    case common::kNetworkMessage:
        return 0 % thread_count_;
    case common::kSyncMessage:
        return 1 % thread_count_;
    case common::kElectMessage:
    case common::kVssMessage:
    case common::kBlsMessage:
        return 2 % thread_count_;
    case common::kBftMessage:
        return 3 % thread_count_;  // get with detail
    case common::kPoolsMessage:
        return GetTxThreadIndex(msg_ptr);
    default:
        return 3 % thread_count_;
    }
}

std::shared_ptr<address::protobuf::AddressInfo> MultiThreadHandler::GetAddressInfo(
        const std::string& addr) {
    // first get from cache
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    if (address_map_.get(addr, &address_info)) {
        return address_info;
    }

    // get from db and add to memory cache
    address_info = prefix_db_->GetAddressInfo(addr);
    if (address_info != nullptr) {
        address_map_.add(addr, address_info);
    }

    return address_info;
}

bool MultiThreadHandler::IsMessageUnique(uint64_t msg_hash) {
    return unique_message_sets_.add(msg_hash);
}
 
MessagePtr MultiThreadHandler::GetMessageFromQueue(uint32_t thread_idx) {
    for (uint32_t i = 0; i < thread_count_; ++i) {
        if (i % thread_count_ == thread_idx) {
            for (uint32_t j = kTransportPrioritySystem; j < kTransportPriorityMaxCount; ++j) {
                if (threads_message_queues_[i][j].size() > 0) {
                    MessagePtr msg_obj;
                    threads_message_queues_[i][j].pop(&msg_obj);
                    return msg_obj;
                }
            }
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
    for (uint32_t i = 0; i < thread_count_; ++i) {
        delete[] threads_message_queues_[i];
    }

    delete[] threads_message_queues_;
    delete[] wait_con_;
    delete[] wait_mutex_;
}

void MultiThreadHandler::InitThreadPriorityMessageQueues() {
    threads_message_queues_ = new common::ThreadSafeQueue<MessagePtr>*[thread_count_];
    for (uint32_t i = 0; i < thread_count_; ++i) {
        threads_message_queues_[i] =
            new common::ThreadSafeQueue<MessagePtr>[kTransportPriorityMaxCount];
    }
}

}  // namespace transport

}  // namespace zjchain
