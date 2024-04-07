#include "consensus/hotstuff/view_block_chain_syncer.h"
#include <common/global_info.h>
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
namespace consensus {

ViewBlockChainSyncer::ViewBlockChainSyncer() {
    // start consumeloop thread
    network::Route::Instance()->RegisterMessage(common::kViewBlockMessage,
        std::bind(&ViewBlockChainSyncer::HandleMessage, this, std::placeholders::_1));
    tick_.CutOff(100000lu, std::bind(&ViewBlockChainSyncer::ConsensusTimerMessage, this));
}

ViewBlockChainSyncer::~ViewBlockChainSyncer() {}

void ViewBlockChainSyncer::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // TODO 放入消息队列，等待消费
    auto header = msg_ptr->header;
    assert(header.type() == common::kViewBlockMessage);

    consume_queue_.push(msg_ptr);
}

// 批量异步处理，提高 tps
void ViewBlockChainSyncer::ConsensusTimerMessage() {
    SyncChains();
    ConsumeMessages();

    tick_.CutOff(
        100000lu,
        std::bind(&ViewBlockChainSyncer::ConsensusTimerMessage, this));    
}

void ViewBlockChainSyncer::SyncChains() {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        auto vb_msg = view_block::protobuf::ViewBlockMessage();
        auto req = vb_msg.mutable_view_block_req();
        req->set_pool_idx(pool_idx);
        req->set_network_id(common::GlobalInfo::Instance()->network_id());
        SendRequest(common::GlobalInfo::Instance()->network_id(), vb_msg);
    }
}

void ViewBlockChainSyncer::ConsumeMessages() {
    // Consume Messages
    while (true) {
        transport::MessagePtr msg_ptr = nullptr;
        if (!consume_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
            break;
        }

        if (msg_ptr->header.view_block_proto().has_view_block_req()) {
            processRequest(msg_ptr);
        } else if (msg_ptr->header.view_block_proto().has_view_block_res()) {
            processResponse(msg_ptr);
        }
    }    
}

Status ViewBlockChainSyncer::SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockMessage& view_block_msg) {
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
    msg.set_type(common::kViewBlockMessage);
    *msg.mutable_view_block_proto() = view_block_msg;

    transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);    
    return Status::kSuccess;
}

// 处理 request 类型消息
Status ViewBlockChainSyncer::processRequest(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_req());

    // TODO 同步全部的 ViewBlockChain，不再按照 hash 同步

    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
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
        view_block_item->set_hash(view_block->hash);
        view_block_item->set_parent_hash(view_block->parent_hash);
        view_block_item->set_leader_idx(view_block->leader_idx);
        view_block_item->set_block_str(view_block->block->SerializeAsString());
        view_block_item->set_qc_str(view_block->qc->Serialize());
        view_block_item->set_view(view_block->view);        
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kViewBlockMessage);
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
        std::shared_ptr<ViewBlock> view_block = nullptr;
        std::shared_ptr<block::protobuf::Block> block_item = nullptr;
        if (block_item->ParseFromString(it->block_str())) {
            std::shared_ptr<QC> qc = nullptr;
            if (qc->Unserialize(it->qc_str())) {
                view_block->block = block_item;
                view_block->qc = qc;
                view_block->hash = it->hash();
                view_block->parent_hash = it->parent_hash();
                view_block->view = it->view();
                view_block->leader_idx = it->leader_idx();

                min_heap.push(view_block);
            }
        }
    }

    if (min_heap.empty()) {
        return Status::kSuccess;
    }

    auto start = min_heap.top();
    min_heap.pop();
    auto sync_chain = std::make_shared<ViewBlockChain>(start);
    
    while (!min_heap.empty()) {
        auto& view_block = min_heap.top();
        min_heap.pop();

        sync_chain->Store(view_block);
    }

    if (!sync_chain->IsValid()) {
        return Status::kSuccess;
    }

    MergeChain(chain, sync_chain);
    
    return Status::kSuccess;
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
            if (StoreViewBlock(sync_block) != Status::kSuccess) {
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

    std::vector<std::shared_ptr<ViewBlock>> sync_all_blocks;
    sync_chain->GetOrderedAll(sync_all_blocks);
    for (auto sync_block : sync_all_blocks) {
        if (StoreViewBlock(sync_block) != Status::kSuccess) {
            break;
        }        
    }
    return Status::kSuccess;
}

Status ViewBlockChainSyncer::StoreViewBlock(const std::shared_ptr<ViewBlock>&) {
    // TODO OnPropose 逻辑
    // 1. 验证 block
    // 2. CommitRule
    // 3. 视图切换    
    return Status::kSuccess;
}

} // namespace consensus

} // namespace shardora

