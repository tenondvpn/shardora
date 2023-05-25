#include "transport/multi_thread.h"

#include <functional>

#include "common/utils.h"
#include "common/global_info.h"
#include "common/time_utils.h"
#include "network//network_utils.h"
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
            auto btime = common::TimeUtils::TimestampUs();
            Processor::Instance()->HandleMessage(msg_ptr);
            ZJC_DEBUG("message handled msg hash: %lu, thread idx: %d", msg_ptr->header.hash64(), msg_ptr->thread_idx);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime >= 100000lu) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_DEBUG("over handle message: %d use: %lu us, all: %s", msg_ptr->header.type(), (etime - btime), t.c_str());
            }

            if (msg_ptr->header.type() == common::kPoolsMessage) {
                ZJC_DEBUG("thread pools message coming.");
            }

        }

        if (thread_idx_ + 1 < common::GlobalInfo::Instance()->message_handler_thread_count()) {
            auto btime = common::TimeUtils::TimestampUs();
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->thread_idx = thread_idx_;
            msg_ptr->header.set_type(common::kConsensusTimerMessage);
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime >= 100000lu) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_DEBUG("0 over handle message: %d use: %lu us, all: %s", msg_ptr->header.type(), (etime - btime), t.c_str());
            }
        }

        if (thread_idx_ + 1 == common::GlobalInfo::Instance()->message_handler_thread_count()) {
            auto btime = common::TimeUtils::TimestampUs();
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->thread_idx = thread_idx_;
            msg_ptr->header.set_type(common::kPoolTimerMessage);
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime >= 100000lu) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_DEBUG("1 over handle message: %d use: %lu us, all: %s", msg_ptr->header.type(), (etime - btime), t.c_str());
            }
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
    ZJC_DEBUG("message coming msg hash: %lu", msg_ptr->header.hash64());
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

    // all key value must temp kv
    db::DbWriteBatch db_batch;
    SaveKeyValue(msg_ptr->header, db_batch);
    if (!db_->Put(db_batch).ok()) {
        ZJC_FATAL("save db failed!");
        return;
    }

    if (msg_ptr->header.type() == common::kSyncMessage) {
        HandleSyncBlockResponse(msg_ptr);
    }

    auto queue_idx = GetThreadIndex(msg_ptr);
    if (queue_idx == consensus_thread_count_ &&
            threads_message_queues_[queue_idx][priority].size() >= kMaxMessageReserveCount) {
        ZJC_WARN("message extend max: %u", kMaxMessageReserveCount);
        return;
    }

    threads_message_queues_[queue_idx][priority].push(msg_ptr);
    wait_con_[queue_idx % all_thread_count_].notify_one();
    if (msg_ptr->header.type() == common::kPoolsMessage) {
        ZJC_DEBUG("pools message coming.");
    }
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
    case common::kInitMessage:
        return consensus_thread_count_;
    case common::kConsensusMessage:
        return msg_ptr->header.zbft().pool_index() % consensus_thread_count_;
    default:
        return consensus_thread_count_;
    }
}

void MultiThreadHandler::HandleSyncBlockResponse(MessagePtr& msg_ptr) {
    if ((uint32_t)msg_ptr->header.src_sharding_id() != common::GlobalInfo::Instance()->network_id() &&
            (uint32_t)msg_ptr->header.src_sharding_id() + network::kConsensusWaitingShardOffset !=
            common::GlobalInfo::Instance()->network_id() &&
            (uint32_t)msg_ptr->header.src_sharding_id() !=
            common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset) {
        return;
    }

    auto& sync_msg = msg_ptr->header.sync_proto();
    if (!sync_msg.has_sync_value_res()) {
        return;
    }

    auto& res_arr = sync_msg.sync_value_res().res();
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        auto block_item = std::make_shared<block::protobuf::Block>();
        if (block_item->ParseFromString(iter->value()) &&
                (iter->has_height() || !block_item->hash().empty())) {
            if (block_item->network_id() != common::GlobalInfo::Instance()->network_id() &&
                    block_item->network_id() + network::kConsensusWaitingShardOffset !=
                    common::GlobalInfo::Instance()->network_id()) {
                continue;
            }

            auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
            auto& msg = new_msg_ptr->header;
            msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
            common::DhtKey dht_key;
            dht_key.construct.net_id = common::GlobalInfo::Instance()->network_id();
            std::string str_key = std::string(dht_key.dht_key, sizeof(dht_key.dht_key));
            msg.set_des_dht_key(str_key);
            msg.set_type(common::kConsensusMessage);
            auto& bft_msg = *msg.mutable_zbft();
            bft_msg.set_sync_block(true);
            bft_msg.set_member_index(-1);
            bft_msg.set_pool_index(block_item->pool_index());
            assert(block_item->has_bls_agg_sign_y() && block_item->has_bls_agg_sign_x());
            *bft_msg.mutable_block() = *block_item;
            auto queue_idx = GetThreadIndex(new_msg_ptr);
            threads_message_queues_[queue_idx][kTransportPriorityHighest].push(new_msg_ptr);
            wait_con_[queue_idx % all_thread_count_].notify_one();
            ZJC_DEBUG("create sync block message: %d, index: %d, queue_idx: %d",
                queue_idx, block_item->pool_index(), queue_idx);
        }
    }
}

void MultiThreadHandler::SaveKeyValue(const transport::protobuf::Header& msg, db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < msg.sync().items_size(); ++i) {
        ZJC_DEBUG("save storage %s, %s",
            common::Encode::HexEncode(msg.sync().items(i).key()).c_str(),
            common::Encode::HexEncode(msg.sync().items(i).value()).c_str());
        prefix_db_->SaveTemporaryKv(
            msg.sync().items(i).key(),
            msg.sync().items(i).value(),
            db_batch);
    }
}

bool MultiThreadHandler::IsMessageUnique(uint64_t msg_hash) {
    bool valid = unique_message_sets_.add(msg_hash);
    if (!valid) {
        ZJC_DEBUG("message filtered: %lu", msg_hash);
    }

    return valid;
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
