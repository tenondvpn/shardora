#include "transport/multi_thread.h"

#include <functional>

#include "common/utils.h"
#include "common/global_info.h"
#include "common/random.h"
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
    uint64_t thread_timer_hash_64 = common::Random::RandomUint64();
    static const uint32_t kMaxHandleMessageCount = 16u;
    while (!destroy_) {
        uint32_t count = 0;
        while (count++ < kMaxHandleMessageCount) {
            auto msg_ptr = msg_handler_->GetMessageFromQueue(thread_idx_);
            if (!msg_ptr) {
                break;
            }

            msg_ptr->thread_idx = thread_idx_;
            ZJC_DEBUG("start message handled msg hash: %lu, thread idx: %d",
                msg_ptr->header.hash64(), msg_ptr->thread_idx);
            msg_ptr->header.set_hop_count(msg_ptr->header.hop_count() + 1);
            auto btime = common::TimeUtils::TimestampUs();
            msg_ptr->times[msg_ptr->times_idx++] = btime;
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime > 200000) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_INFO("over handle message: %d, thread: %d use: %lu us, all: %s",
                    msg_ptr->header.type(), thread_idx_, (etime - btime), t.c_str());
            }
            ZJC_DEBUG("end message handled msg hash: %lu, thread idx: %d", msg_ptr->header.hash64(), msg_ptr->thread_idx);
        }

        if (thread_idx_ + 1 < common::GlobalInfo::Instance()->message_handler_thread_count()) {
            auto btime = common::TimeUtils::TimestampUs();
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->thread_idx = thread_idx_;
            msg_ptr->header.set_type(common::kConsensusTimerMessage);
            ZJC_DEBUG("start kConsensusTimerMessage message handled msg hash: %lu, thread idx: %d", msg_ptr->header.hash64(), msg_ptr->thread_idx);
            msg_ptr->times[msg_ptr->times_idx++] = btime;
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime > 200000) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_INFO("kConsensusTimerMessage over handle message: %d, thread: %d use: %lu us, all: %s", msg_ptr->header.type(), thread_idx_,(etime - btime), t.c_str());
            }
            ZJC_DEBUG("end kConsensusTimerMessage message handled msg hash: %lu, thread idx: %d", msg_ptr->header.hash64(), msg_ptr->thread_idx);
            ++thread_timer_hash_64;
        }

        if (count >= kMaxHandleMessageCount) {
            continue;
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

int32_t MultiThreadHandler::GetPriority(MessagePtr& msg_ptr) {
    auto& msg = msg_ptr->header;
    switch (msg.type()) {
    case common::kConsensusMessage:
        ZJC_DEBUG("get consensus message tx type: %d, prepare: %s, precommit: %s, commit: %s, has_sync: %d",
            msg.zbft().tx_bft().tx_type(),
            common::Encode::HexEncode(msg.zbft().prepare_gid()).c_str(),
            common::Encode::HexEncode(msg.zbft().precommit_gid()).c_str(),
            common::Encode::HexEncode(msg.zbft().commit_gid()).c_str(),
            msg.zbft().sync_block());
        if (msg.zbft().tx_bft().tx_type() != pools::protobuf::kNormalFrom &&
                msg.zbft().tx_bft().tx_type() != pools::protobuf::kNormalTo) {
            return kTransportPrioritySystem;
        }

        if (msg.zbft().pool_index() == common::kImmutablePoolSize) {
            return kTransportPriorityHighest;
        }

        if (!msg.zbft().commit_gid().empty()) {
            return kTransportPriorityHighest;
        }

        if (!msg.zbft().prepare_gid().empty() && msg.zbft().leader_idx() < 0) {
            msg_ptr->handle_timeout = common::TimeUtils::TimestampMs() + 2 * kHandledTimeoutMs;
            return kTransportPriorityHigh;
        }
       
        if (!msg.zbft().precommit_gid().empty()) {
//             msg_ptr->handle_timeout = common::TimeUtils::TimestampMs() + 2 * kHandledTimeoutMs;
            return kTransportPriorityHigh;
        }

        if (!msg.zbft().prepare_gid().empty()) {
            msg_ptr->handle_timeout = common::TimeUtils::TimestampMs() + kHandledTimeoutMs;
            return kTransportPriorityMiddle;
        }

        return kTransportPriorityLow;
    case common::kPoolsMessage:
        return kTransportPriorityHigh;
    case common::kInitMessage:
        return kTransportPriorityHighest;
    case common::kBlsMessage:
        return kTransportPrioritySystem;
    case common::kElectMessage:
    case common::kVssMessage:
        return kTransportPriorityHigh;
    default:
        return kTransportPriorityLowest;
    }
}

void MultiThreadHandler::HandleMessage(MessagePtr& msg_ptr) {
    if (common::kConsensusMessage == msg_ptr->header.type()) {
        if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
            return;
        }

        if ((uint32_t)msg_ptr->header.src_sharding_id() != common::GlobalInfo::Instance()->network_id() &&
                (uint32_t)msg_ptr->header.src_sharding_id() + network::kConsensusWaitingShardOffset !=
                common::GlobalInfo::Instance()->network_id() &&
                (uint32_t)msg_ptr->header.src_sharding_id() !=
                common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset) {
            return;
        }
    }

    uint32_t priority = GetPriority(msg_ptr);
    if (thread_vec_.empty()) {
        return;
    }

    if (msg_ptr->header.hop_count() >= kMaxHops) {
        return;
    }

    if (!IsMessageUnique(msg_ptr->header.hash64())) {
        ZJC_DEBUG("message filtered: %lu", msg_ptr->header.hash64());
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
    if (queue_idx > consensus_thread_count_) {
        assert(false);
        return;
    }

    if (queue_idx == consensus_thread_count_ &&
            threads_message_queues_[queue_idx][priority].size() >= kMaxMessageReserveCount) {
        ZJC_WARN("message extend max: %u", kMaxMessageReserveCount);
        return;
    }

    threads_message_queues_[queue_idx][priority].push(msg_ptr);
    wait_con_[queue_idx % all_thread_count_].notify_one();
    ZJC_DEBUG("queue size message push success: %lu, queue_idx: %d, priority: %d, thread queue size: %u, net: %u, type: %d",
        msg_ptr->header.hash64(), queue_idx, priority,
        threads_message_queues_[queue_idx][priority].size(),
        common::GlobalInfo::Instance()->network_id(),
        msg_ptr->header.type());
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
        if (msg_ptr->header.zbft().pool_index() < common::kInvalidPoolIndex) {
            return common::GlobalInfo::Instance()->pools_with_thread()[msg_ptr->header.zbft().pool_index()];
        }

        assert(false);
        return consensus_thread_count_ + 1;
    default:
        return consensus_thread_count_;
    }
}

void MultiThreadHandler::HandleSyncBlockResponse(MessagePtr& msg_ptr) {
    ZJC_DEBUG("sync response coming.");
    if ((uint32_t)msg_ptr->header.src_sharding_id() != common::GlobalInfo::Instance()->network_id() &&
            (uint32_t)msg_ptr->header.src_sharding_id() + network::kConsensusWaitingShardOffset !=
            common::GlobalInfo::Instance()->network_id() &&
            (uint32_t)msg_ptr->header.src_sharding_id() !=
            common::GlobalInfo::Instance()->network_id() + network::kConsensusWaitingShardOffset) {
        ZJC_DEBUG("sync response coming net error: %u, %u", msg_ptr->header.src_sharding_id(), common::GlobalInfo::Instance()->network_id());
        return;
    }

    auto& sync_msg = msg_ptr->header.sync_proto();
    if (!sync_msg.has_sync_value_res()) {
        ZJC_DEBUG("not has sync value res.");
        return;
    }

    auto& res_arr = sync_msg.sync_value_res().res();
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        auto block_item = std::make_shared<block::protobuf::Block>();
        if (block_item->ParseFromString(iter->value()) &&
                (iter->has_height() || !block_item->hash().empty())) {
            if (prefix_db_->BlockExists(block_item->hash())) {
                ZJC_DEBUG("block hash exists not has sync value res: %s",
                    common::Encode::HexEncode(block_item->hash()).c_str());
                continue;
            }

            if (block_item->network_id() != common::GlobalInfo::Instance()->network_id() &&
                    block_item->network_id() + network::kConsensusWaitingShardOffset !=
                    common::GlobalInfo::Instance()->network_id()) {
                ZJC_DEBUG("sync response coming net error:  %u, %u, %u",
                    block_item->network_id(), msg_ptr->header.src_sharding_id(), common::GlobalInfo::Instance()->network_id());
                continue;
            }
            
            auto new_msg_ptr = std::make_shared<transport::TransportMessage>();
            CreateConsensusBlockMessage(new_msg_ptr, block_item);
        }
    }
}

void MultiThreadHandler::CreateConsensusBlockMessage(
        std::shared_ptr<transport::TransportMessage>& new_msg_ptr,
        std::shared_ptr<block::protobuf::Block>& block_item) {
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
    if (queue_idx > consensus_thread_count_) {
        assert(false);
        return;
    }

    transport::TcpTransport::Instance()->SetMessageHash(new_msg_ptr->header, queue_idx);
    uint32_t priority = GetPriority(new_msg_ptr);
    threads_message_queues_[queue_idx][priority].push(new_msg_ptr);
    ZJC_DEBUG("create sync block message: %d, index: %d, queue_idx: %d, hash64: %lu, block hash: %s, size: %u",
        queue_idx, block_item->pool_index(), queue_idx, new_msg_ptr->header.hash64(),
        common::Encode::HexEncode(block_item->hash()).c_str(),
        threads_message_queues_[queue_idx][priority].size());
    wait_con_[queue_idx % all_thread_count_].notify_one();
}

void MultiThreadHandler::SaveKeyValue(const transport::protobuf::Header& msg, db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < msg.sync().items_size(); ++i) {
//         ZJC_DEBUG("save storage %s, %s",
//             common::Encode::HexEncode(msg.sync().items(i).key()).c_str(),
//             common::Encode::HexEncode(msg.sync().items(i).value()).c_str());
        prefix_db_->SaveTemporaryKv(
            msg.sync().items(i).key(),
            msg.sync().items(i).value(),
            db_batch);
    }
}

bool MultiThreadHandler::IsMessageUnique(uint64_t msg_hash) {
    return unique_message_sets_.add(msg_hash);
}
 
MessagePtr MultiThreadHandler::GetMessageFromQueue(uint32_t thread_idx) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    for (uint32_t i = 0; i < all_thread_count_; ++i) {
        if (i % all_thread_count_ == thread_idx) {
            for (uint32_t pri = kTransportPrioritySystem; pri < kTransportPriorityMaxCount; ++pri) {
                if (threads_message_queues_[i][pri].size() > 0) {
                    MessagePtr msg_obj;
                    threads_message_queues_[i][pri].pop(&msg_obj);
                    if (msg_obj->handle_timeout < now_tm_ms) {
                        ZJC_DEBUG("remove handle timeout invalid message hash: %lu", msg_obj->header.hash64());
                        continue;
                    }

                    ZJC_DEBUG("pop valid message hash: %lu, size: %u, thread: %u",
                        msg_obj->header.hash64(), threads_message_queues_[i][pri].size(), thread_idx);
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
