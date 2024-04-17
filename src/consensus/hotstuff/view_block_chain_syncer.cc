#include "consensus/hotstuff/view_block_chain_syncer.h"
#include <common/encode.h>
#include <common/global_info.h>
#include <common/log.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/types.h>
#include <dht/dht_function.h>
#include <dht/dht_key.h>
#include <dht/dht_utils.h>
#include <memory>
#include <network/network_utils.h>
#include <network/universal_manager.h>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "network/route.h"
#include "protos/view_block.pb.h"
#include "consensus/hotstuff/view_block_chain.h"

namespace shardora {

namespace hotstuff {

ViewBlockChainSyncer::ViewBlockChainSyncer(const std::shared_ptr<ViewBlockChainManager>& c_mgr) : view_block_chain_mgr_(c_mgr) {
    // start consumeloop thread
    network::Route::Instance()->RegisterMessage(common::kViewBlockSyncMessage,
        std::bind(&ViewBlockChainSyncer::HandleMessage, this, std::placeholders::_1));
}

ViewBlockChainSyncer::~ViewBlockChainSyncer() {}

void ViewBlockChainSyncer::Start() {
    tick_.CutOff(kSyncTimerCycleUs, std::bind(&ViewBlockChainSyncer::ConsensusTimerMessage, this));
}

void ViewBlockChainSyncer::Stop() { tick_.Destroy(); }

int ViewBlockChainSyncer::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void ViewBlockChainSyncer::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto header = msg_ptr->header;
    assert(header.type() == common::kViewBlockSyncMessage);
    
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    consume_queues_[thread_idx].push(msg_ptr);
}

// 批量异步处理，提高 tps
void ViewBlockChainSyncer::ConsensusTimerMessage() {
    SyncChains();
    ConsumeMessages();

    tick_.CutOff(
        kSyncTimerCycleUs,
        std::bind(&ViewBlockChainSyncer::ConsensusTimerMessage, this));    
}

void ViewBlockChainSyncer::SyncChains() {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
        auto req = vb_msg.mutable_view_block_req();
        req->set_pool_idx(pool_idx);
        req->set_network_id(common::GlobalInfo::Instance()->network_id());
        vb_msg.set_create_time_us(common::TimeUtils::TimestampUs());
        SendRequest(common::GlobalInfo::Instance()->network_id(), vb_msg);
    }
}

void ViewBlockChainSyncer::ConsumeMessages() {
    // Consume Messages
    uint32_t pop_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (pop_count++ < kPopCountMax) {
            transport::MessagePtr msg_ptr = nullptr;
            consume_queues_[thread_idx].pop(&msg_ptr);
            if (msg_ptr == nullptr) {
                break;
            }
            if (msg_ptr->header.view_block_proto().has_view_block_req()) {
                processRequest(msg_ptr);
            } else if (msg_ptr->header.view_block_proto().has_view_block_res()) {
                processResponse(msg_ptr);
            }
        }   
    }
}

Status ViewBlockChainSyncer::SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockSyncMessage& view_block_msg) {
    // 只有共识池节点才能同步 ViewBlock
    if (network_id >= network::kConsensusWaitingShardBeginNetworkId) {
        return Status::kError;
    }
    // 获取邻居节点
    std::vector<dht::NodePtr> nodes;
    auto dht_ptr = network::UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
    auto dht = *dht_ptr->readonly_hash_sort_dht();
    dht::DhtFunction::GetNetworkNodes(dht, network_id, nodes);

    if (nodes.empty()) {
        return Status::kError;
    }
    dht::NodePtr node = nodes[rand() % nodes.size()];

    transport::protobuf::Header msg;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(network_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kViewBlockSyncMessage);
    *msg.mutable_view_block_proto() = view_block_msg;
    
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);    
    return Status::kSuccess;
}

// 处理 request 类型消息
Status ViewBlockChainSyncer::processRequest(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_req());

    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockSyncMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    res_view_block_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    auto view_block_res = res_view_block_msg.mutable_view_block_res();
    uint32_t pool_idx = view_block_msg.view_block_req().pool_idx();
    view_block_res->set_network_id(view_block_msg.view_block_req().network_id());
    view_block_res->set_pool_idx(pool_idx);

    auto chain = view_block_chain_mgr_->Chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    std::vector<std::shared_ptr<ViewBlock>> all;
    chain->GetAll(all);

    for (auto& view_block : all) {
        auto view_block_item = view_block_res->add_view_block_items();
        ViewBlock2Proto(view_block, view_block_item);
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kViewBlockSyncMessage);
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    transport::TcpTransport::Instance()->Send(msg_ptr->conn.get(), msg);
    
    return Status::kSuccess;
}

// 处理 response 类型消息
Status ViewBlockChainSyncer::processResponse(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_res());
    auto& view_block_items = view_block_msg.view_block_res().view_block_items();
    uint32_t pool_idx = view_block_msg.view_block_res().pool_idx();

    auto chain = view_block_chain_mgr_->Chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    

    ViewBlockMinHeap min_heap;
    for (auto it = view_block_items.begin(); it != view_block_items.end(); it++) {
        auto view_block = std::make_shared<ViewBlock>();
        Status s = Proto2ViewBlock(*it, view_block);
        if (s != Status::kSuccess) {
            return s;
        }
        min_heap.push(view_block);        
    }

    if (min_heap.empty()) {
        return Status::kSuccess;
    }
    
    auto sync_chain = std::make_shared<ViewBlockChain>();
    while (!min_heap.empty()) {
        auto view_block = min_heap.top();
        min_heap.pop();

        sync_chain->Store(view_block);
    }

    ZJC_DEBUG("Sync blocks to chain, pool_idx: %d, view_blocks: %d",
        pool_idx, view_block_items.size());

    if (!sync_chain->IsValid()) {
        return Status::kSuccess;
    }

    return MergeChain(chain, sync_chain);
}

Status ViewBlockChainSyncer::MergeChain(std::shared_ptr<ViewBlockChain>& ori_chain, const std::shared_ptr<ViewBlockChain>& sync_chain) {
    // 寻找交点
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    ori_chain->GetAll(view_blocks);
    std::shared_ptr<ViewBlock> cross_block = nullptr;
    for (auto it = view_blocks.begin(); it != view_blocks.end(); it++) {
        sync_chain->Get((*it)->hash, cross_block);
        if (cross_block) {
            break;
        }
    }

    // 两条链存在交点，则从交点之后开始 merge 
    if (cross_block) {
        std::vector<std::shared_ptr<ViewBlock>> sync_all_blocks;
        sync_chain->GetOrderedAll(sync_all_blocks);

        for (auto sync_block : sync_all_blocks) {
            if (sync_block->view <= cross_block->view) {
                continue;
            }
            if (ori_chain->Has(sync_block->hash)) {
                continue;
            }
            if (on_recv_vb_fn_ && on_recv_vb_fn_(ori_chain, sync_block) != Status::kSuccess) {
                break;
            }
        }
        
        return Status::kSuccess;
    }

    // 两条链不存在交点，则替换为 max_view 更大的链
    auto ori_max_height = ori_chain->GetMaxHeight();
    auto sync_max_height = sync_chain->GetMaxHeight();
    if (ori_max_height >= sync_max_height) {
        return Status::kSuccess;
    }

    ori_chain->Clear();
    std::vector<std::shared_ptr<ViewBlock>> sync_all_blocks;
    sync_chain->GetOrderedAll(sync_all_blocks);
    for (auto it = sync_all_blocks.begin(); it != sync_all_blocks.end(); it++) {
        if (on_recv_vb_fn_ && on_recv_vb_fn_(ori_chain, *it) != Status::kSuccess) {
            break;
        }
    }
    return Status::kSuccess;
}

} // namespace consensus

} // namespace shardora

