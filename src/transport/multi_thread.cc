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

namespace shardora {

namespace transport {

ThreadHandler::ThreadHandler(
        MultiThreadHandler* msg_handler,
        std::condition_variable& wait_con,
        std::mutex& wait_mutex)
        : msg_handler_(msg_handler),
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
    static const uint32_t kMaxHandleMessageCount = 16u;
    uint8_t thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    uint8_t maping_thread_idx = common::GlobalInfo::Instance()->SetConsensusRealThreadIdx(thread_idx);
    ZJC_DEBUG("thread handler thread index coming thread_idx: %d, "
        "maping_thread_idx: %d, message_handler_thread_count: %d", 
        thread_idx, maping_thread_idx, 
        common::GlobalInfo::Instance()->message_handler_thread_count());
    msg_handler_->ThreadWaitNotify();
    while (!destroy_) {
        if (!common::GlobalInfo::Instance()->main_inited_success()) {
            usleep(100000);
            continue;
        }
        
        uint32_t count = 0;
        while (count++ < kMaxHandleMessageCount) {
            auto btime = common::TimeUtils::TimestampUs();
            auto msg_ptr = msg_handler_->GetMessageFromQueue(
                thread_idx, 
                (maping_thread_idx == (common::GlobalInfo::Instance()->message_handler_thread_count() - 1)));
            if (!msg_ptr) {
                auto etime = common::TimeUtils::TimestampUs();
                if (etime - btime > 200000) {
                    std::string t;
                    ZJC_INFO("0 over handle thread: %d use: %lu us, all: %s", thread_idx, (etime - btime), t.c_str());
                }
                break;
            }

            // ZJC_DEBUG("start message handled msg hash: %lu, thread idx: %d",
            //     msg_ptr->header.hash64(), thread_idx);
            msg_ptr->times_idx = 0;
            msg_ptr->header.set_hop_count(msg_ptr->header.hop_count() + 1);
            msg_ptr->thread_index = thread_idx;
            ADD_DEBUG_PROCESS_TIMESTAMP();
            Processor::Instance()->HandleMessage(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime > 1000000) {
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    if (msg_ptr->times[i] > 500000) {
                        ZJC_DEBUG("over handle message debug %lu timestamp: %lu, debug: %s, "
                            "thread_idx: %d, maping_thread_idx: %d",
                            msg_ptr->header.hash64(), msg_ptr->times[i], 
                            msg_ptr->debug_str[i].c_str(), thread_idx, maping_thread_idx);
                    }
                }
            }
            // ZJC_DEBUG("end message handled msg hash: %lu, thread idx: %d", msg_ptr->header.hash64(), thread_idx);
        }

        auto btime = common::TimeUtils::TimestampUs();
        if (maping_thread_idx != common::GlobalInfo::Instance()->message_handler_thread_count() - 1) {
#ifndef ENABLE_HOTSTUFF            
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->header.set_type(common::kConsensusTimerMessage);
            // ZJC_DEBUG("start kConsensusTimerMessage message handled msg hash: %lu, thread idx: %d, maping: %d", 
            //     msg_ptr->header.hash64(), thread_idx, maping_thread_idx);
            msg_ptr->times[msg_ptr->times_idx++] = btime;
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime > 200000) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_INFO("kConsensusTimerMessage over handle message: %d, thread: %d use: %lu us, all: %s", 
                    msg_ptr->header.type(), thread_idx, (etime - btime), t.c_str());
            }

            // ZJC_DEBUG("over kConsensusTimerMessage message handled msg hash: %lu, thread idx: %d, maping: %d", 
            //     msg_ptr->header.hash64(), thread_idx, maping_thread_idx);

#else
            // HotstuffSyncTimerMessage
            auto btime = common::TimeUtils::TimestampUs();
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->header.set_type(common::kHotstuffSyncTimerMessage);
            msg_ptr->times[msg_ptr->times_idx++] = btime;
            Processor::Instance()->HandleMessage(msg_ptr);
            // PacemakerTimerMessage
            btime = common::TimeUtils::TimestampUs();
            msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->header.set_type(common::kPacemakerTimerMessage);
            msg_ptr->times[msg_ptr->times_idx++] = btime;
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            // ZJC_INFO("kPacemakerTimerMessage over handle message: %d, thread: %d use: %lu us", 
            //     msg_ptr->header.type(), thread_idx, (etime - btime));            
#endif            
        } else {
            auto btime = common::TimeUtils::TimestampUs();
            auto msg_ptr = std::make_shared<transport::TransportMessage>();
            msg_ptr->header.set_type(common::kPoolTimerMessage);
            // ZJC_DEBUG("start kConsensusTimerMessage message handled msg hash: %lu, thread idx: %d, maping: %d", 
            //     msg_ptr->header.hash64(), thread_idx, maping_thread_idx);
            msg_ptr->times[msg_ptr->times_idx++] = btime;
            Processor::Instance()->HandleMessage(msg_ptr);
            auto etime = common::TimeUtils::TimestampUs();
            if (etime - btime > 200000) {
                std::string t;
                for (uint32_t i = 1; i < msg_ptr->times_idx; ++i) {
                    t += std::to_string(msg_ptr->times[i] - msg_ptr->times[i - 1]) + " ";
                }

                ZJC_INFO("kConsensusTimerMessage over handle message: %d, thread: %d use: %lu us, all: %s", 
                    msg_ptr->header.type(), thread_idx, (etime - btime), t.c_str());
            }
            // ZJC_DEBUG("end kConsensusTimerMessage message handled msg hash: %lu, thread idx: %d, maping: %d", 
            //     msg_ptr->header.hash64(), thread_idx, maping_thread_idx);
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

int MultiThreadHandler::Init(std::shared_ptr<db::Db>& db, std::shared_ptr<security::Security>& security) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    security_ = security;
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
        thread_vec_.push_back(std::make_shared<ThreadHandler>(this, wait_con_[i], wait_mutex_[i]));
        std::unique_lock<std::mutex> lock(thread_wait_mutex_);
        thread_wait_con_.wait(lock);
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
        if (msg.zbft().sync_block()) {
            return kTransportPriorityHighest;
        }

        if (msg.zbft().tx_bft().tx_type() != pools::protobuf::kNormalFrom &&
                msg.zbft().tx_bft().tx_type() != pools::protobuf::kNormalTo) {
            return kTransportPrioritySystem;
        }

        if (msg.zbft().pool_index() == common::kImmutablePoolSize) {
            return kTransportPriorityHighest;
        }

        if (!msg.zbft().commit_gid().empty()) {
            return kTransportPriorityLow;
        }

        if (!msg.zbft().prepare_gid().empty()) {
            msg_ptr->handle_timeout = common::TimeUtils::TimestampMs() + 2 * kHandledTimeoutMs;
            return kTransportPriorityMiddle;
        }
       
        if (!msg.zbft().precommit_gid().empty()) {
            msg_ptr->handle_timeout = common::TimeUtils::TimestampMs() + 2 * kHandledTimeoutMs;
            if (msg.zbft().leader_idx() > 0) {
                return kTransportPriorityHigh;
            }

            return kTransportPriorityHigh;
        }

        return kTransportPriorityLow;
    case common::kHotstuffMessage:
    case common::kPoolsMessage:
        return kTransportPrioritySystem;
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

    if (CheckMessageValid(msg_ptr) != kFirewallCheckSuccess) {
        if (msg_ptr->conn) {
            ZJC_DEBUG("message filtered: %lu, type: %d, from: %s:%d",
                msg_ptr->header.hash64(),
                msg_ptr->header.type(),
                msg_ptr->conn->PeerIp().c_str(),
                msg_ptr->conn->PeerPort());
        } else {
            ZJC_DEBUG("message filtered: %lu, type: %d, from: %s:%d",
                msg_ptr->header.hash64(),
                msg_ptr->header.type(),
                "local_ip",
                0);
        }
        
        return;
    }

    // if (msg_ptr->header.type() == common::kSyncMessage) {
    //     HandleSyncBlockResponse(msg_ptr);
    // }

    if (msg_ptr->header.type() == common::kConsensusMessage && 
            msg_ptr->header.zbft().bft_timeout() && 
            msg_ptr->header.zbft().leader_idx() != -1) {
        HandleSyncBftTimeout(msg_ptr);
        return;
    }

    auto thread_index = GetThreadIndex(msg_ptr);
    if (thread_index >= common::kMaxThreadCount) {
        assert(false);
        return;
    }

    threads_message_queues_[thread_index][priority].push(msg_ptr);
    wait_con_[thread_index % all_thread_count_].notify_one();
    ZJC_DEBUG("queue size message push success: %lu, queue_idx: %d, "
        "priority: %d, thread queue size: %u, net: %u, type: %d",
        msg_ptr->header.hash64(), thread_index, priority,
        threads_message_queues_[thread_index][priority].size(),
        common::GlobalInfo::Instance()->network_id(),
        msg_ptr->header.type());
}

uint8_t MultiThreadHandler::GetThreadIndex(MessagePtr& msg_ptr) {
#ifndef NDEBUG
    ++msg_type_count_[msg_ptr->header.type()];
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (prev_log_msg_type_tm_ < now_tm_ms) {
        std::string debug_str;
        for (uint32_t i = 0; i < common::kMaxMessageTypeCount; ++i) {
            debug_str += std::to_string(i) + ":" + std::to_string(msg_type_count_[i]) + ", ";
        }

        ZJC_INFO("get msg count: %s", debug_str.c_str());
        memset(msg_type_count_, 0, sizeof(msg_type_count_));
        prev_log_msg_type_tm_ = now_tm_ms + 3000lu;
    }
#endif

    switch (msg_ptr->header.type()) {
    case common::kDhtMessage:
    case common::kNetworkMessage:
    case common::kSyncMessage:
    case common::kElectMessage:
    case common::kVssMessage:
    case common::kBlsMessage:
    case common::kPoolsMessage:
    case common::kInitMessage:
        return common::GlobalInfo::Instance()->get_consensus_thread_idx(consensus_thread_count_);
    case common::kConsensusMessage:
        if (msg_ptr->header.zbft().pool_index() < common::kInvalidPoolIndex) {
            return common::GlobalInfo::Instance()->pools_with_thread()[msg_ptr->header.zbft().pool_index()];
        }

        ZJC_FATAL("invalid message thread: %d", msg_ptr->header.zbft().pool_index());
        return common::kMaxThreadCount;
    case common::kHotstuffSyncMessage:
        if (msg_ptr->header.view_block_proto().has_view_block_req()) {
            return common::GlobalInfo::Instance()->pools_with_thread()[
                    msg_ptr->header.view_block_proto().view_block_req().pool_idx()];
        }
        if (msg_ptr->header.view_block_proto().has_single_req()) {
            return common::GlobalInfo::Instance()->pools_with_thread()[
                    msg_ptr->header.view_block_proto().single_req().pool_idx()];
        }        
        if (msg_ptr->header.view_block_proto().has_view_block_res()) {
            return common::GlobalInfo::Instance()->pools_with_thread()[
                    msg_ptr->header.view_block_proto().view_block_res().pool_idx()];
        }
        return common::kMaxThreadCount;
    case common::kHotstuffMessage:
        if (msg_ptr->header.hotstuff().pool_index() < common::kInvalidPoolIndex) {
            return common::GlobalInfo::Instance()->pools_with_thread()[msg_ptr->header.hotstuff().pool_index()];
        }
        return common::kMaxThreadCount;
    case common::kHotstuffTimeoutMessage:
        if (msg_ptr->header.hotstuff_timeout_proto().pool_idx() < common::kInvalidPoolIndex) {
            return common::GlobalInfo::Instance()->pools_with_thread()[msg_ptr->header.hotstuff_timeout_proto().pool_idx()];
        }
        return common::kMaxThreadCount;
    default:
        return common::GlobalInfo::Instance()->get_consensus_thread_idx(consensus_thread_count_);
    }
}

void MultiThreadHandler::HandleSyncBftTimeout(MessagePtr& msg_ptr) {
    ZJC_DEBUG("success get pool bft timeout hash64: %lu", msg_ptr->header.hash64());
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
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
        bft_msg.set_leader_idx(msg_ptr->header.zbft().leader_idx());
        bft_msg.set_pool_index(i);
        bft_msg.set_bft_timeout(true);
        auto queue_idx = GetThreadIndex(new_msg_ptr);
        if (queue_idx >= common::kMaxThreadCount) {
            assert(false);
            return;
        }

        ZJC_DEBUG("success handle pool: %u, bft timeout hash64: %lu", i, msg_ptr->header.hash64());
        transport::TcpTransport::Instance()->SetMessageHash(new_msg_ptr->header);
        uint32_t priority = GetPriority(new_msg_ptr);
        threads_message_queues_[queue_idx][priority].push(new_msg_ptr);
        wait_con_[queue_idx % all_thread_count_].notify_one();
    }
}

void MultiThreadHandler::SaveKeyValue(const transport::protobuf::Header& msg, db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < msg.sync().items_size(); ++i) {
        assert(false);
        // prefix_db_->SaveTemporaryKv(
        //     msg.sync().items(i).key(),
        //     msg.sync().items(i).value(),
        //     db_batch);
    }
}

bool MultiThreadHandler::IsMessageUnique(uint64_t msg_hash) {
    // for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
    //     uint64_t hash64;
    //     while (local_broadcast_messages_[i].pop(&hash64)) {
    //         unique_message_sets2_.Push(hash64);
    //     }
    // }

    return unique_message_sets2_.Push(msg_hash);
    
    // return unique_message_sets_.add(msg_hash);
}

bool MultiThreadHandler::IsFromMessageUnique(const std::string& from_ip, uint64_t msg_hash) {
    auto from_hash = common::Hash::Hash64(from_ip + ":" + std::to_string(msg_hash));
    return from_unique_message_sets_.Push(from_hash);
}

int MultiThreadHandler::CheckMessageValid(MessagePtr& msg_ptr) {
    // if (!IsFromMessageUnique(msg_ptr->conn->PeerIp(), msg_ptr->header.hash64())) {
    //     return kFirewallCheckError;
    // }

    if (!IsMessageUnique(msg_ptr->header.hash64())) {
        // invalid msg id
        if (msg_ptr->conn) {
            ZJC_DEBUG("check message id failed %d, %lu, from: %s:%d",
                msg_ptr->header.type(), msg_ptr->header.hash64(),
                msg_ptr->conn->PeerIp().c_str(), msg_ptr->conn->PeerPort());
        } else {
            ZJC_DEBUG("check message id failed %d, %lu, from: %s:%d",
                msg_ptr->header.type(), msg_ptr->header.hash64(),
                "local_ip", 0);
        }

        return kFirewallCheckError;
    }

    if (msg_ptr->header.type() >= common::kMaxMessageTypeCount) {
        ZJC_DEBUG("invalid message type: %d", msg_ptr->header.type());
        return kFirewallCheckError;
    }

    if (firewall_checks_[msg_ptr->header.type()] == nullptr) {
        ZJC_DEBUG("invalid fierwall check message type: %d", msg_ptr->header.type());
        return kFirewallCheckSuccess;
    }

    int check_status = firewall_checks_[msg_ptr->header.type()](msg_ptr);
    if (check_status != kFirewallCheckSuccess) {
        ZJC_DEBUG("check firewall failed %d", msg_ptr->header.type());
        return kFirewallCheckError;
    }

    return kFirewallCheckSuccess;
}

int MultiThreadHandler::CheckSignValid(MessagePtr& msg_ptr) {
    if (!msg_ptr->header.has_sign() || !msg_ptr->header.has_pubkey() ||
            msg_ptr->header.sign().empty() || msg_ptr->header.pubkey().empty()) {
        ZJC_DEBUG("invalid message no sign or no public key.");
        return kFirewallCheckError;
    }

    std::string sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_->Verify(
            sign_hash,
            msg_ptr->header.pubkey(),
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        ZJC_ERROR("verify signature failed!");
        return kFirewallCheckError;
    }

    return kFirewallCheckSuccess;
}

int MultiThreadHandler::CheckDhtMessageValid(MessagePtr& msg_ptr) {
    if (CheckSignValid(msg_ptr) != kFirewallCheckSuccess) {
        ZJC_DEBUG("check dht msg failed!");
        return kFirewallCheckError;
    }

    ZJC_DEBUG("check dht msg success!");
    return kFirewallCheckSuccess;
}

MessagePtr MultiThreadHandler::GetMessageFromQueue(uint32_t thread_idx, bool http_svr_thread) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    for (uint32_t pri = kTransportPrioritySystem; pri < kTransportPriorityMaxCount; ++pri) {
        MessagePtr msg_obj;
        threads_message_queues_[thread_idx][pri].pop(&msg_obj);
        if (msg_obj == nullptr) {
            continue;
        }

        if (msg_obj->handle_timeout < now_tm_ms) {
            ZJC_DEBUG("remove handle timeout invalid message hash: %lu", msg_obj->header.hash64());
            continue;
        }

        // ZJC_DEBUG("pop valid message hash: %lu, size: %u, thread: %u",
        //     msg_obj->header.hash64(), threads_message_queues_[thread_idx][pri].size(), thread_idx);
        return msg_obj;
    }

    // handle http/ws request
    if (http_svr_thread) {
        MessagePtr msg_obj;
        http_server_message_queue_.pop(&msg_obj);
        // if (msg_obj != nullptr) {
        //     ZJC_DEBUG("get msg http transaction success %s, %s, hash64: %lu, step: %d, gid: %s, type: %d", 
        //         common::Encode::HexEncode(
        //         security_->GetAddress(msg_obj->header.tx_proto().pubkey())).c_str(),
        //         common::Encode::HexEncode(msg_obj->header.tx_proto().to()).c_str(),
        //         msg_obj->header.hash64(),
        //         msg_obj->header.tx_proto().step(),
        //         common::Encode::HexEncode(msg_obj->header.tx_proto().gid()).c_str(),
        //         msg_obj->header.type());
        // }
        return msg_obj;
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
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        delete[] threads_message_queues_[i];
    }

    delete[] threads_message_queues_;
    delete[] wait_con_;
    delete[] wait_mutex_;
}

void MultiThreadHandler::InitThreadPriorityMessageQueues() {
    threads_message_queues_ = new common::ThreadSafeQueue<MessagePtr>*[common::kMaxThreadCount];
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        threads_message_queues_[i] =
            new common::ThreadSafeQueue<MessagePtr>[kTransportPriorityMaxCount];
    }
}

}  // namespace transport

}  // namespace shardora
