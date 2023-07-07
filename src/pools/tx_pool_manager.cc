#include "pools/tx_pool_manager.h"

#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/secp256k1.h"
#include "transport/processor.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace pools {

TxPoolManager::TxPoolManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        RotationLeaderCallback rotatition_leader_cb) {
    security_ = security;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    prefix_db_->InitGidManager();
    kv_sync_ = kv_sync;
    rotatition_leader_cb_ = rotatition_leader_cb;
    cross_block_mgr_ = std::make_shared<CrossBlockManager>(db_, kv_sync_);
    tx_pool_ = new TxPool[common::kInvalidPoolIndex];
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].Init(i, db, kv_sync);
    }

    ZJC_INFO("TxPoolManager init success: %d", common::kInvalidPoolIndex);
    InitCrossPools();
    tick_.CutOff(
        10000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this, std::placeholders::_1));
    network::Route::Instance()->RegisterMessage(
        common::kPoolsMessage,
        std::bind(&TxPoolManager::HandleMessage, this, std::placeholders::_1));
}

TxPoolManager::~TxPoolManager() {
    FlushHeightTree();
    if (tx_pool_ != nullptr) {
        delete []tx_pool_;
    }

    if (cross_pools_ != nullptr) {
        delete[] cross_pools_;
    }

    prefix_db_->Destroy();
}

void TxPoolManager::InitCrossPools() {
    uint32_t got_sharding_id = common::kInvalidUint32;
    uint32_t des_sharding_id = common::kInvalidUint32;
    if (!prefix_db_->GetJoinShard(&got_sharding_id, &des_sharding_id)) {
        return;
    }

    bool local_is_root = false;
    if (got_sharding_id != common::kInvalidUint32) {
        if (got_sharding_id == network::kRootCongressNetworkId ||
                got_sharding_id ==
                network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset) {
            local_is_root = true;
        }
    }

    if (local_is_root) {
        cross_pools_ = new CrossPool[network::kConsensusWaitingShardOffset];
        for (uint32_t i = network::kConsensusShardBeginNetworkId;
                i < network::kConsensusShardEndNetworkId; ++i) {
            cross_pools_[i - network::kConsensusShardBeginNetworkId].Init(i, db_, kv_sync_);
        }

        max_cross_pools_size_ = network::kConsensusWaitingShardOffset;
    } else {
        cross_pools_ = new CrossPool[1];
        cross_pools_[0].Init(network::kRootCongressNetworkId, db_, kv_sync_);
    }

    ZJC_DEBUG("init cross pool success local_is_root: %d, got_sharding_id: %u, des_sharding_id: %u",
        local_is_root, got_sharding_id, des_sharding_id);
}

void TxPoolManager::SyncCrossPool(uint8_t thread_idx) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    if (max_cross_pools_size_ == 1) {
        auto sync_count = cross_pools_[0].SyncMissingBlocks(thread_idx, now_tm_ms);
        uint64_t ex_height = common::kInvalidUint64;
        if (cross_pools_[0].latest_height() == common::kInvalidUint64) {
            ex_height = 1;
        } else {
            if (cross_pools_[0].latest_height() < cross_synced_max_heights_[0]) {
                ex_height = cross_pools_[0].latest_height() + 1;
            }
        }

        if (ex_height != common::kInvalidUint64) {
            uint32_t count = 0;
            for (uint64_t i = ex_height; i < cross_synced_max_heights_[0] && count < 64; ++i, ++count) {
                kv_sync_->AddSyncHeight(
                    thread_idx,
                    network::kRootCongressNetworkId,
                    common::kRootChainPoolIndex,
                    i,
                    sync::kSyncHigh);
            }
        }

        ZJC_DEBUG("cross success sync mising heights pool: %u, height: %lu, "
            "max height: %lu, des max height: %lu, sync_count: %d",
            0,
            0,
            cross_pools_[0].latest_height(),
            ex_height,
            sync_count);
        return;
    }

    prev_cross_sync_index_ %= now_sharding_count_;
    auto begin_pool = prev_cross_sync_index_;
    for (; prev_cross_sync_index_ < now_sharding_count_; ++prev_cross_sync_index_) {
        auto res = cross_pools_[prev_cross_sync_index_].SyncMissingBlocks(thread_idx, now_tm_ms);
        if (cross_pools_[prev_cross_sync_index_].latest_height() == common::kInvalidUint64 ||
                cross_pools_[prev_cross_sync_index_].latest_height() <
                cross_synced_max_heights_[prev_cross_sync_index_]) {
            kv_sync_->AddSyncHeight(
                thread_idx,
                network::kConsensusShardBeginNetworkId + prev_cross_sync_index_,
                common::kRootChainPoolIndex,
                cross_synced_max_heights_[prev_cross_sync_index_],
                sync::kSyncHigh);
            ZJC_DEBUG("max cross success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_cross_sync_index_,
                res, cross_pools_[prev_cross_sync_index_].latest_height(),
                cross_synced_max_heights_[prev_synced_pool_index_]);
        }

        if (res > 0) {
            ZJC_DEBUG("cross success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_cross_sync_index_,
                res, cross_pools_[prev_cross_sync_index_].latest_height(),
                cross_synced_max_heights_[prev_synced_pool_index_]);
            ++prev_cross_sync_index_;
            return;
        }
    }

    for (prev_cross_sync_index_ = 0; prev_cross_sync_index_ < begin_pool; ++prev_cross_sync_index_) {
        auto res = cross_pools_[prev_cross_sync_index_].SyncMissingBlocks(thread_idx, now_tm_ms);
        if (cross_pools_[prev_cross_sync_index_].latest_height() == common::kInvalidUint64 ||
                cross_pools_[prev_cross_sync_index_].latest_height() <
                cross_synced_max_heights_[prev_cross_sync_index_]) {
            kv_sync_->AddSyncHeight(
                thread_idx,
                network::kConsensusShardBeginNetworkId + prev_cross_sync_index_,
                common::kRootChainPoolIndex,
                cross_synced_max_heights_[prev_cross_sync_index_],
                sync::kSyncHigh);
            ZJC_DEBUG("max cross success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_cross_sync_index_,
                res, cross_pools_[prev_cross_sync_index_].latest_height(),
                cross_synced_max_heights_[prev_synced_pool_index_]);
        }

        if (res > 0) {
            ZJC_DEBUG("cross success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_cross_sync_index_,
                res, cross_pools_[prev_cross_sync_index_].latest_height(),
                cross_synced_max_heights_[prev_synced_pool_index_]);
            ++prev_cross_sync_index_;
            return;
        }
    }
}

void TxPoolManager::FlushHeightTree() {
    db::DbWriteBatch db_batch;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].FlushHeightTree(db_batch);
    }

    if (cross_pools_ != nullptr) {
        if (max_cross_pools_size_ == 1) {
            cross_pools_[0].FlushHeightTree(db_batch);
        } else {
            for (uint32_t i = 0; i < now_sharding_count_; ++i) {
                cross_pools_[i].FlushHeightTree(db_batch);
            }
        }
    }

    ZJC_DEBUG("success call FlushHeightTree");
    if (!db_->Put(db_batch).ok()) {
        ZJC_FATAL("write db failed!");
    }
}

void TxPoolManager::ConsensusTimerMessage(uint8_t thread_idx) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("tx pool manager called timer: %lu, thread_idx: %lu", now_tm_ms, thread_idx);
    PopPoolsMessage(thread_idx);
    if (prev_sync_height_tree_tm_ms_ < now_tm_ms) {
        FlushHeightTree();
        prev_sync_height_tree_tm_ms_ = now_tm_ms + kFlushHeightTreePeriod;
    }

    if (prev_check_leader_valid_ms_ < now_tm_ms) {
        if (network::DhtManager::Instance()->valid_count(
                common::GlobalInfo::Instance()->network_id()) >=
                common::GlobalInfo::Instance()->sharding_min_nodes_count()) {
            bool get_factor = false;
            if (prev_cacultate_leader_valid_ms_ < now_tm_ms) {
                get_factor = true;
                prev_cacultate_leader_valid_ms_ = now_tm_ms + kCaculateLeaderLofPeriod;
            }

            std::vector<double> factors(common::kInvalidPoolIndex);
            for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
                double res = tx_pool_[i].CheckLeaderValid(get_factor);
                if (get_factor) {
                    if (res > 0) {
                        factors[i] = 1.0;
                    } else {
                        factors[i] = res;
                    }

                    ZJC_DEBUG("get_factor: %d, get invalid pool factor pool: %d, factor: %f",
                        get_factor, i, res);
                }
            }

            if (get_factor) {
                std::vector<int32_t> invalid_pools;
                invalid_pools.reserve(64);
                CheckLeaderValid(factors, &invalid_pools);
                ZJC_DEBUG("invalid_pools.size(): %d", invalid_pools.size());
                if (!invalid_pools.empty() && rotatition_leader_cb_ != nullptr) {
                    rotatition_leader_cb_(thread_idx, invalid_pools);
                }
            }

            prev_check_leader_valid_ms_ = now_tm_ms + kCheckLeaderLofPeriod;
        }
    }

    if (prev_sync_check_ms_ < now_tm_ms) {
        SyncMinssingHeights(thread_idx, now_tm_ms);
        prev_sync_check_ms_ = now_tm_ms + kSyncMissingBlockPeriod;
    }

    if (prev_sync_heights_ms_ < now_tm_ms) {
        SyncPoolsMaxHeight(thread_idx);
        prev_sync_heights_ms_ = now_tm_ms + kSyncPoolsMaxHeightsPeriod;
    }

    if (prev_sync_cross_ms_ < now_tm_ms) {
        if (cross_pools_ == nullptr) {
            InitCrossPools();
        } else {
            SyncCrossPool(thread_idx);
        }

        if (max_cross_pools_size_ > 1) {
            prev_sync_cross_ms_ = now_tm_ms + kSyncCrossPeriod;
        } else {
            prev_sync_cross_ms_ = now_tm_ms + kSyncCrossPeriod / now_sharding_count_;
        }
    }

    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 10) {
        ZJC_DEBUG("TxPoolManager handle message use time: %lu", (etime - now_tm_ms));
    }

    ZJC_DEBUG("end tx pool manager called timer: %lu, thread_idx: %u", now_tm_ms, thread_idx);
    tick_.CutOff(
        100000lu,
        std::bind(&TxPoolManager::ConsensusTimerMessage, this, std::placeholders::_1));
}

void TxPoolManager::CheckLeaderValid(
        const std::vector<double>& factors,
        std::vector<int32_t>* invalid_pools) {
    double average = 0.0;
    for (uint32_t i = 0; i < factors.size(); ++i) {
        average += factors[i];
    }

    if (average <= 0.0) {
        // all leader invalid
        if (latest_leader_count_ <= 2) {
            invalid_pools->push_back(-1);
        }

        return;
    }

    average /= factors.size();
    double variance = 0.0;
    for (uint32_t i = 0; i < factors.size(); ++i) {
        variance += (factors[i] - average) * (factors[i] - average);
    }

    variance = sqrt(variance / (factors.size() - 1));
    for (uint32_t i = 0; i < factors.size(); ++i) {
        double grubbs = abs(factors[i] - average) / variance;
        if (grubbs > kGrubbsValidFactor && factors[i] < kInvalidLeaderRatio) {
            // invalid leader
            ZJC_DEBUG("invalid pool found grubbs: %f, %d", grubbs, i);
            invalid_pools->push_back(i);
        }
    }
}

void TxPoolManager::SyncMinssingHeights(uint8_t thread_idx, uint64_t now_tm_ms) {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    prev_synced_pool_index_ %= common::kInvalidPoolIndex;
    auto begin_pool = prev_synced_pool_index_;
    for (; prev_synced_pool_index_ < common::kInvalidPoolIndex; ++prev_synced_pool_index_) {
        auto res = tx_pool_[prev_synced_pool_index_].SyncMissingBlocks(thread_idx, now_tm_ms);
        if (tx_pool_[prev_synced_pool_index_].latest_height() == common::kInvalidUint64 ||
                tx_pool_[prev_synced_pool_index_].latest_height() <
                synced_max_heights_[prev_synced_pool_index_]) {
            SyncBlockWithMaxHeights(
                thread_idx, prev_synced_pool_index_, synced_max_heights_[prev_synced_pool_index_]);
            ZJC_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, tx_pool_[prev_synced_pool_index_].latest_height(),
                synced_max_heights_[prev_synced_pool_index_]);
        }

        if (res > 0) {
            ZJC_DEBUG("success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, tx_pool_[prev_synced_pool_index_].latest_height(),
                synced_max_heights_[prev_synced_pool_index_]);
        }
    }

    for (prev_synced_pool_index_ = 0;
            prev_synced_pool_index_ < begin_pool; ++prev_synced_pool_index_) {
        auto res = tx_pool_[prev_synced_pool_index_].SyncMissingBlocks(thread_idx, now_tm_ms);
        if (tx_pool_[prev_synced_pool_index_].latest_height() == common::kInvalidUint64 || 
                tx_pool_[prev_synced_pool_index_].latest_height() <
                synced_max_heights_[prev_synced_pool_index_]) {
            SyncBlockWithMaxHeights(
                thread_idx, prev_synced_pool_index_, synced_max_heights_[prev_synced_pool_index_]);
            ZJC_DEBUG("max success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, tx_pool_[prev_synced_pool_index_].latest_height(),
                synced_max_heights_[prev_synced_pool_index_]);
        }

        if (res > 0) {
            ZJC_DEBUG("success sync mising heights pool: %u, height: %lu, max height: %lu, des max height: %lu",
                prev_synced_pool_index_,
                res, tx_pool_[prev_synced_pool_index_].latest_height(),
                synced_max_heights_[prev_synced_pool_index_]);
        }
    }
}

void TxPoolManager::SyncBlockWithMaxHeights(uint8_t thread_idx, uint32_t pool_idx, uint64_t height) {
    if (kv_sync_ == nullptr) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    kv_sync_->AddSyncHeight(
        thread_idx,
        net_id,
        pool_idx,
        height,
        sync::kSyncHigh);
}

std::shared_ptr<address::protobuf::AddressInfo> TxPoolManager::GetAddressInfo(
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

void TxPoolManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // just one thread
    assert(msg_ptr->thread_idx < common::kMaxThreadCount);
    pools_msg_queue_[msg_ptr->thread_idx].push(msg_ptr);
    ZJC_DEBUG("queue size msg_ptr->thread_idx: %d, pools_msg_queue_: %d",
        msg_ptr->thread_idx, pools_msg_queue_[msg_ptr->thread_idx].size());
    assert(pools_msg_queue_[msg_ptr->thread_idx].size() < 512);
}

void TxPoolManager::PopPoolsMessage(uint8_t thread_idx) {
    for (int32_t i = 0; i < common::kMaxThreadCount; ++i) {
        while (pools_msg_queue_[i].size() > 0) {
            transport::MessagePtr msg_ptr = nullptr;
            if (!pools_msg_queue_[i].pop(&msg_ptr)) {
                break;
            }

            msg_ptr->thread_idx = thread_idx;
            HandlePoolsMessage(msg_ptr);
        }
    }
}

void TxPoolManager::HandlePoolsMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    if (header.has_tx_proto()) {
        auto& tx_msg = header.tx_proto();
        switch (tx_msg.step()) {
        case pools::protobuf::kJoinElect:
            HandleElectTx(msg_ptr);
            break;
        case pools::protobuf::kNormalFrom:
            HandleNormalFromTx(msg_ptr);
            break;
        case pools::protobuf::kCreateLibrary:
        case pools::protobuf::kContractCreate:
            HandleCreateContractTx(msg_ptr);
            break;
        case pools::protobuf::kContractGasPrepayment:
            HandleSetContractPrepayment(msg_ptr);
            break;
        case pools::protobuf::kRootCreateAddress: {
            if (tx_msg.to().size() != security::kUnicastAddressLength) {
                return;
            }

            // must not coming from network
            if (msg_ptr->conn != nullptr) {
                return;
            }

            auto pool_index = common::GetAddressPoolIndex(tx_msg.to()) % common::kImmutablePoolSize;
            msg_queues_[pool_index].push(msg_ptr);
            ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", pool_index, msg_queues_[pool_index].size());
            break;
        }
        case pools::protobuf::kContractExcute:
            HandleContractExcute(msg_ptr);
            break;
        case pools::protobuf::kConsensusLocalTos: {
            auto pool_index = common::GetAddressPoolIndex(tx_msg.to());
            msg_queues_[pool_index].push(msg_ptr);
            ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", pool_index, msg_queues_[pool_index].size());
            break;
        }
        case pools::protobuf::kRootCross: {
            auto pool_index = common::GetAddressPoolIndex(tx_msg.to());
            msg_queues_[pool_index].push(msg_ptr);
            ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", pool_index, msg_queues_[pool_index].size());
            break;
        }
        default:
            ZJC_DEBUG("invalid tx step: %d", tx_msg.step());
            assert(false);
            break;
        }
    }

    if (header.has_sync_heights()) {
        HandleSyncPoolsMaxHeight(msg_ptr);
    }
}

void TxPoolManager::SyncPoolsMaxHeight(uint8_t thread_idx) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    msg_ptr->header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    dht::DhtKeyManager dht_key(net_id);
    msg_ptr->header.set_des_dht_key(dht_key.StrKey());
    msg_ptr->header.set_type(common::kPoolsMessage);
    auto* sync_heights = msg_ptr->header.mutable_sync_heights();
    sync_heights->set_req(true);
    transport::TcpTransport::Instance()->SetMessageHash(
        msg_ptr->header,
        msg_ptr->thread_idx);
    network::Route::Instance()->Send(msg_ptr);
}

void TxPoolManager::HandleSyncPoolsMaxHeight(const transport::MessagePtr& msg_ptr) {
    if (tx_pool_ == nullptr) {
        return;
    }

    if (!msg_ptr->header.has_sync_heights()) {
        return;
    }

    if (msg_ptr->header.sync_heights().req()) {
        transport::protobuf::Header msg;
        msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
        dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kPoolsMessage);
        auto* sync_heights = msg.mutable_sync_heights();
        uint32_t pool_idx = common::kInvalidPoolIndex;
        std::string sync_debug;
        std::string cross_debug;
        for (uint32_t i = 0; i < pool_idx; ++i) {
            sync_heights->add_heights(tx_pool_[i].latest_height());
            sync_debug += std::to_string(tx_pool_[i].latest_height()) + " ";
        }

        if (max_cross_pools_size_ == 1) {
            sync_heights->add_cross_heights(cross_pools_[0].latest_height());
            cross_debug += std::to_string(cross_pools_[0].latest_height()) + " ";
        } else {
            for (uint32_t i = 0; i < now_sharding_count_; ++i) {
                sync_heights->add_cross_heights(cross_pools_[i].latest_height());
                cross_debug += std::to_string(cross_pools_[i].latest_height()) + " ";
            }
        }

        transport::TcpTransport::Instance()->SetMessageHash(
            msg,
            msg_ptr->thread_idx);
        transport::TcpTransport::Instance()->Send(msg_ptr->thread_idx, msg_ptr->conn, msg);
        ZJC_DEBUG("response pool heights: %s, cross pool heights: %s", sync_debug.c_str(), cross_debug.c_str());
    } else {
        auto& heights = msg_ptr->header.sync_heights().heights();
        if (heights.size() != common::kInvalidPoolIndex) {
            return;
        }

        std::string sync_debug;
        std::string cross_debug;
        for (int32_t i = 0; i < heights.size(); ++i) {
            sync_debug += std::to_string(i) + "_" + std::to_string(heights[i]) + " ";
            if (heights[i] != common::kInvalidUint64) {
                if (tx_pool_[i].latest_height() == common::kInvalidUint64 &&
                        synced_max_heights_[i] < heights[i]) {
                    synced_max_heights_[i] = heights[i];
                    continue;
                }

                if (heights[i] > tx_pool_[i].latest_height() + 64) {
                    synced_max_heights_[i] = tx_pool_[i].latest_height() + 64;
                    continue;
                }

                if (heights[i] > tx_pool_[i].latest_height()) {
                    synced_max_heights_[i] = heights[i];
                }
            }
        }

        auto& cross_heights = msg_ptr->header.sync_heights().cross_heights();
        for (int32_t i = 0; i < cross_heights.size(); ++i) {
            if (max_cross_pools_size_ == 1) {
                if (cross_heights.size() != 1) {
                    assert(false);
                    break;
                }
            } else {
                if (cross_heights.size() != network::kConsensusWaitingShardOffset) {
                    assert(false);
                    break;
                }
            }

            cross_debug += std::to_string(cross_heights[i]) + " ";
            if (cross_heights[i] != common::kInvalidUint64) {
                uint64_t update_height = common::kInvalidUint64;
                do {
                    if (cross_pools_[i].latest_height() == common::kInvalidUint64 &&
                            cross_synced_max_heights_[i] < cross_heights[i]) {
                        update_height = cross_heights[i];
                        break;
                    }

                    if (cross_heights[i] > cross_pools_[i].latest_height() + 64) {
                        update_height = cross_pools_[i].latest_height() + 64;
                        break;
                    }

                    if (cross_heights[i] > cross_pools_[i].latest_height()) {
                        update_height = cross_heights[i];
                        break;
                    }
                } while (0);
                
                ZJC_DEBUG("get response pool heights: %s, cross pool heights: %s, update_height: %lu, "
                    "cross_synced_max_heights_[i]: %lu, cross_pools_[i].latest_height(): %lu, cross_heights[i]: %lu",
                    sync_debug.c_str(), cross_debug.c_str(), update_height,
                    cross_synced_max_heights_[i], cross_pools_[i].latest_height(),
                    cross_heights[i]);
                if (update_height != common::kInvalidUint64) {
                    cross_synced_max_heights_[i] = cross_heights[i];
                    if (max_cross_pools_size_ == 1) {
                        cross_block_mgr_->UpdateMaxHeight(network::kRootCongressNetworkId, cross_heights[i]);
                    } else {
                        cross_block_mgr_->UpdateMaxHeight(i + network::kConsensusShardBeginNetworkId, cross_heights[i]);
                    }
                }
            }
        }

        ZJC_DEBUG("get response pool heights: %s, cross pool heights: %s", sync_debug.c_str(), cross_debug.c_str());
    }
}

void TxPoolManager::HandleElectTx(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = *header.mutable_tx_proto();
    auto addr = security_->GetAddress(tx_msg.pubkey());
    msg_ptr->address_info = GetAddressInfo(addr);
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info: %s", common::Encode::HexEncode(addr).c_str());
        return;
    }

    if (msg_ptr->address_info->addr() == tx_msg.to()) {
        assert(false);
        return;
    }

    if (msg_ptr->address_info->balance() < consensus::kJoinElectGas) {
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (!tx_msg.has_key() || tx_msg.key() != protos::kElectJoinShard) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return;
    }

    auto msg_hash = pools::GetTxMessageHash(tx_msg);
    if (security_->Verify(
            msg_hash,
            tx_msg.pubkey(),
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        ZJC_WARN("kElectJoin verify signature failed!");
        return;
    }

    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(tx_msg.value())) {
        return;
    }

    uint32_t tmp_shard = join_info.shard_id();
    if (tmp_shard != network::kRootCongressNetworkId) {
        if (tmp_shard != msg_ptr->address_info->sharding_id()) {
            ZJC_DEBUG("join des shard error: %d,  %d.",
                tmp_shard, msg_ptr->address_info->sharding_id());
            return;
        }
    }

    std::string new_hash;
    if (!SaveNodeVerfiyVec(msg_ptr->address_info->addr(), join_info, &new_hash)) {
        assert(false);
        return;
    }

    tx_msg.set_key(protos::kJoinElectVerifyG2);
    tx_msg.set_value(new_hash);
    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (prefix_db_->GidExists(msg_ptr->msg_hash)) {
        // avoid save gid different tx
        ZJC_DEBUG("tx msg hash exists: %s failed!",
            common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
        return;
    }

    if (prefix_db_->GidExists(tx_msg.gid())) {
        ZJC_DEBUG("tx gid exists: %s failed!", common::Encode::HexEncode(tx_msg.gid()).c_str());
        return;
    }

    prefix_db_->SaveAddressPubkey(msg_ptr->address_info->addr(), tx_msg.pubkey());
    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", msg_ptr->address_info->pool_index(), msg_queues_[msg_ptr->address_info->pool_index()].size());
    ZJC_DEBUG("success add elect tx has verify g2: %d", tx_msg.has_key());
}

bool TxPoolManager::SaveNodeVerfiyVec(
        const std::string& id,
        const bls::protobuf::JoinElectInfo& join_info,
        std::string* new_hash) {
    int32_t t = common::GetSignerCount(common::GlobalInfo::Instance()->each_shard_max_members());
    if (join_info.g2_req().verify_vec_size() > 0 && join_info.g2_req().verify_vec_size() != t) {
        return false;
    }

    std::string str_for_hash;
    str_for_hash.reserve(join_info.g2_req().verify_vec_size() * 4 * 64 + 8);
    uint32_t shard_id = join_info.shard_id();
    uint32_t mem_idx = join_info.member_idx();
    str_for_hash.append((char*)&shard_id, sizeof(shard_id));
    str_for_hash.append((char*)&mem_idx, sizeof(mem_idx));
    for (int32_t i = 0; i < join_info.g2_req().verify_vec_size(); ++i) {
        auto& item = join_info.g2_req().verify_vec(i);
        str_for_hash.append(item.x_c0());
        str_for_hash.append(item.x_c1());
        str_for_hash.append(item.y_c0());
        str_for_hash.append(item.y_c1());
        str_for_hash.append(item.z_c0());
        str_for_hash.append(item.z_c1());
    }

    *new_hash = common::Hash::keccak256(str_for_hash);
    auto str = join_info.SerializeAsString();
    prefix_db_->SaveTemporaryKv(*new_hash, str);
    return true;
}

void TxPoolManager::HandleContractExcute(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    if (tx_msg.has_key() && tx_msg.key().size() > 0) {
        ZJC_DEBUG("call contract key must empty.");
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (tx_msg.gas_price() <= 0 || tx_msg.gas_limit() <= consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("gas price and gas limit error %lu, %lu",
            tx_msg.gas_price(), tx_msg.gas_limit());
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    msg_ptr->address_info = GetAddressInfo(tx_msg.to());
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no contract address info: %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    auto from = security_->GetAddress(tx_msg.pubkey());
    if (msg_ptr->address_info->addr() == from) {
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    uint64_t height = 0;
    uint64_t prepayment = 0;
    if (!prefix_db_->GetContractUserPrepayment(
            tx_msg.to(),
            from,
            &height,
            &prepayment)) {
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (prepayment < tx_msg.amount() + tx_msg.gas_limit() * tx_msg.gas_price()) {
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (prefix_db_->GidExists(msg_ptr->msg_hash)) {
        // avoid save gid different tx
        ZJC_DEBUG("tx msg hash exists: %s failed!",
            common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    if (prefix_db_->GidExists(tx_msg.gid())) {
        ZJC_DEBUG("tx gid exists: %s failed!", common::Encode::HexEncode(tx_msg.gid()).c_str());
        ZJC_DEBUG("failed add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", msg_ptr->address_info->pool_index(), msg_queues_[msg_ptr->address_info->pool_index()].size());
    //     ZJC_INFO("success add contract call. %s", common::Encode::HexEncode(tx_msg.to()).c_str());
}

void TxPoolManager::HandleSetContractPrepayment(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    // user can't direct call contract, pay contract prepayment and call contract direct
    if (!tx_msg.contract_input().empty() ||
            tx_msg.contract_prepayment() < consensus::kCallContractDefaultUseGas) {
        ZJC_DEBUG("call contract not has valid contract input"
            "and contract prepayment invalid.");
        return;
    }

    if (!UserTxValid(msg_ptr)) {
        return;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kCallContractDefaultUseGas * tx_msg.gas_price()) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            consensus::kCallContractDefaultUseGas);
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", msg_ptr->address_info->pool_index(), msg_queues_[msg_ptr->address_info->pool_index()].size());
}

bool TxPoolManager::UserTxValid(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& tx_msg = header.tx_proto();
    msg_ptr->address_info = GetAddressInfo(security_->GetAddress(tx_msg.pubkey()));
    if (msg_ptr->address_info == nullptr) {
        ZJC_WARN("no address info.");
        return false;
    }

    if (msg_ptr->address_info->addr() == tx_msg.to()) {
        assert(false);
        return false;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_WARN("sharding error id: %s, shard: %d, local shard: %d",
            common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        assert(false);
        return false;
    }

    if (tx_msg.has_key() && tx_msg.key().size() > kTxStorageKeyMaxSize) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return false;
    }

    msg_ptr->msg_hash = pools::GetTxMessageHash(tx_msg);
    if (prefix_db_->GidExists(msg_ptr->msg_hash)) {
        // avoid save gid different tx
        ZJC_DEBUG("tx msg hash exists: %s failed!",
            common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
        return false;
    }

    if (prefix_db_->GidExists(tx_msg.gid())) {
        ZJC_DEBUG("tx gid exists: %s failed!", common::Encode::HexEncode(tx_msg.gid()).c_str());
        return false;
    }

    return true;
}

void TxPoolManager::HandleNormalFromTx(const transport::MessagePtr& msg_ptr) {
    if (!UserTxValid(msg_ptr)) {
        assert(false);
        return;
    }

    auto& tx_msg = msg_ptr->header.tx_proto();
    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            consensus::kTransferGas * tx_msg.gas_price()) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            consensus::kCallContractDefaultUseGas);
        assert(false);
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", msg_ptr->address_info->pool_index(), msg_queues_[msg_ptr->address_info->pool_index()].size());
    ZJC_DEBUG("success push tx: %s, %lu", common::Encode::HexEncode(tx_msg.gid()).c_str(), msg_ptr->header.hash64());
}

void TxPoolManager::HandleCreateContractTx(const transport::MessagePtr& msg_ptr) {
    auto& tx_msg = msg_ptr->header.tx_proto();
    if (!tx_msg.has_contract_code()) {
        ZJC_DEBUG("create contract not has valid contract code: %s",
            common::Encode::HexEncode(tx_msg.contract_code()).c_str());
        return;
    }

    uint64_t default_gas = consensus::kCallContractDefaultUseGas +
        tx_msg.value().size() * consensus::kKeyValueStorageEachBytes;
    if (tx_msg.step() == pools::protobuf::kContractCreate) {
        if (memcmp(
                tx_msg.contract_code().c_str(),
                protos::kContractBytesStartCode.c_str(),
                protos::kContractBytesStartCode.size()) != 0) {
            return;
        }
    } else {
        // all shards will save the library
        default_gas = consensus::kCreateLibraryDefaultUseGas +
            network::kConsensusWaitingShardOffset *
            (tx_msg.value().size() + tx_msg.key().size()) *
            consensus::kKeyValueStorageEachBytes;
    }

    if (!UserTxValid(msg_ptr)) {
        ZJC_DEBUG("create contract error!");
        return;
    }

    if (msg_ptr->address_info->balance() <
            tx_msg.amount() + tx_msg.contract_prepayment() +
            default_gas * tx_msg.gas_price()) {
        ZJC_DEBUG("address balance invalid: %lu, transfer amount: %lu, "
            "prepayment: %lu, default call contract gas: %lu, gas price: %lu",
            msg_ptr->address_info->balance(),
            tx_msg.amount(),
            tx_msg.contract_prepayment(),
            default_gas,
            tx_msg.gas_price());
        return;
    }

    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
    ZJC_DEBUG("queue index pool_index: %u, msg_queues_: %d", msg_ptr->address_info->pool_index(), msg_queues_[msg_ptr->address_info->pool_index()].size());
    ZJC_INFO("address balance success: %lu, transfer amount: %lu, "
        "prepayment: %lu, default call contract gas: %lu, gas price: %lu, conract bytes: %s",
        msg_ptr->address_info->balance(),
        tx_msg.amount(),
        tx_msg.contract_prepayment(),
        default_gas,
        tx_msg.gas_price(),
        common::Encode::HexEncode(tx_msg.contract_code()).c_str());
}

void TxPoolManager::PopTxs(uint32_t pool_index) {
    uint32_t count = 0;
    while (msg_queues_[pool_index].size() > 0 && ++count < kPopMessageCountEachTime) {
        transport::MessagePtr msg_ptr = nullptr;
        msg_queues_[pool_index].pop(&msg_ptr);
        auto& tx_msg = msg_ptr->header.tx_proto();
        if (tx_msg.step() == pools::protobuf::kNormalFrom) {
            if (security_->Verify(
                    msg_ptr->msg_hash,
                    tx_msg.pubkey(),
                    msg_ptr->header.sign()) != security::kSecuritySuccess) {
                ZJC_WARN("verify signature failed!");
                continue;
            }
        }

        DispatchTx(pool_index, msg_ptr);
        ZJC_DEBUG("success pop tx: %s, %lu", common::Encode::HexEncode(tx_msg.gid()).c_str(), msg_ptr->header.hash64());
    }
}

void TxPoolManager::DispatchTx(uint32_t pool_index, transport::MessagePtr& msg_ptr) {
    if (msg_ptr->header.tx_proto().step() >= pools::protobuf::StepType_ARRAYSIZE) {
        assert(false);
        return;
    }

    if (item_functions_[msg_ptr->header.tx_proto().step()] == nullptr) {
        ZJC_DEBUG("not registered step : %d", msg_ptr->header.tx_proto().step());
        assert(false);
        return;
    }

    pools::TxItemPtr tx_ptr = item_functions_[msg_ptr->header.tx_proto().step()](msg_ptr);
    if (tx_ptr == nullptr) {
        assert(false);
        return;
    }

    tx_pool_[pool_index].AddTx(tx_ptr);
    ZJC_DEBUG("success add local transfer to tx %u, %s, gid: %s, from pk: %s, to: %s",
        pool_index,
        common::Encode::HexEncode(tx_ptr->tx_hash).c_str(),
        common::Encode::HexEncode(tx_ptr->gid).c_str(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().pubkey()).c_str(),
        common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
}

void TxPoolManager::GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map) {
    if (count > common::kSingleBlockMaxTransactions) {
        count = common::kSingleBlockMaxTransactions;
    }
       
    tx_pool_[pool_index].GetTx(res_map, count);
}

void TxPoolManager::TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxRecover(recover_txs);
}

void TxPoolManager::TxOver(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxOver(tx_list);
}

}  // namespace pools

}  // namespace zjchain
