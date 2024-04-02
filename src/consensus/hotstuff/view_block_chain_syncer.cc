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

ViewBlockChainSyncer::ViewBlockChainSyncer(FetchCallbackFn* fetch_cb_) {
    fetch_callback_fn_ = fetch_cb_;
    // start consumeloop thread
    network::Route::Instance()->RegisterMessage(common::kViewBlockMessage,
        std::bind(&ViewBlockChainSyncer::HandleMessage, this, std::placeholders::_1));
    tick_.CutOff(100000lu, std::bind(&ViewBlockChainSyncer::ConsensusTimerMessage, this));
    
}

ViewBlockChainSyncer::~ViewBlockChainSyncer() {}

Status ViewBlockChainSyncer::AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx) {
    auto block_item = std::make_shared<ViewBlockItem>(view_block_hash, pool_idx);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    input_queues_[thread_idx].push(block_item);
    return Status::kSuccess;
}

void ViewBlockChainSyncer::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // TODO 放入消息队列，等待消费
    auto header = msg_ptr->header;
    assert(header.type() == common::kViewBlockMessage);

    consume_queue_.push(msg_ptr);
}

// 批量异步处理，提高 tps
void ViewBlockChainSyncer::ConsensusTimerMessage() {
    produceMessages();
    consumeMessages();

    tick_.CutOff(
        100000lu,
        std::bind(&ViewBlockChainSyncer::ConsensusTimerMessage, this));    
}

void ViewBlockChainSyncer::produceMessages() {
    // 消息消费线程，根据类型分发给不同的方法
    uint32_t pop_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; thread_idx++) {
        while (pop_count++) {
            std::shared_ptr<ViewBlockItem> block_item = nullptr;
            input_queues_[thread_idx].pop(&block_item);
            if (!block_item) {
                break;
            }
            
            item_queue_.push(block_item);
        }
    }

    std::unordered_map<uint32_t, view_block::protobuf::ViewBlockMessage> pool_msg_map;
    while (!item_queue_.empty()) {
        std::shared_ptr<ViewBlockItem> block_item = item_queue_.front();
        item_queue_.pop();
        
        auto it = pool_msg_map.find(block_item->pool_idx);
        if (it == pool_msg_map.end()) {
            pool_msg_map[block_item->pool_idx] = view_block::protobuf::ViewBlockMessage();
        }

        auto req = pool_msg_map[block_item->pool_idx].mutable_view_block_req();
        req->add_hashes(block_item->hash);
        req->set_pool_idx(block_item->pool_idx);
        req->set_network_id(common::GlobalInfo::Instance()->network_id());

        // batch send request
        if (req->hashes_size() > kEachRequestMaxViewBlocksCount) {
            sendRequest(common::GlobalInfo::Instance()->network_id(),
                pool_msg_map[block_item->pool_idx]);

            req->clear_hashes();
        }
    }
}

void ViewBlockChainSyncer::consumeMessages() {
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

Status ViewBlockChainSyncer::sendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockMessage& view_block_msg) {
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

    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    auto view_block_res = res_view_block_msg.mutable_view_block_res();
    view_block_res->set_network_id(view_block_msg.view_block_req().network_id());
    view_block_res->set_pool_idx(view_block_msg.view_block_req().pool_idx());

    for (uint32_t i = 0; i < view_block_msg.view_block_req().hashes_size(); i++) {
        std::string hash = view_block_msg.view_block_req().hashes(i);
        uint32_t pool_idx = view_block_msg.view_block_req().pool_idx();
        // TODO Get view block by hash and pool
        auto view_block_chain = std::make_shared<ViewBlockChain>();
        GetViewBlockChain(pool_idx, view_block_chain);
        if (!view_block_chain) {
            continue;
        }
        auto view_block = std::make_shared<ViewBlock>();
        view_block_chain->Get(hash, view_block);
        if (!view_block) {
            continue;
        }
        
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

    for (auto it = view_block_items.begin(); it != view_block_items.end(); it++) {
        auto view_block = std::make_shared<ViewBlock>();
        auto block_item = std::make_shared<block::protobuf::Block>();
        if (block_item->ParseFromString(it->block_str())) {
            auto qc = std::make_shared<QC>();
            if (qc->Unserialize(it->qc_str())) {
                view_block->block = block_item;
                view_block->qc = qc;
                view_block->hash = it->hash();
                view_block->parent_hash = it->parent_hash();
                view_block->view = it->view();
                view_block->leader_idx = it->leader_idx();
                
                // TODO 根据 pool 获取 block chain
                auto view_block_chain = std::make_shared<ViewBlockChain>();
                view_block_chain->Store(view_block);
            }
        }
    }
    return Status::kSuccess;
}

Status ViewBlockChainSyncer::GetViewBlockChain(uint32_t pool_idx, std::shared_ptr<ViewBlockChain>& view_block_chain) {
    return Status::kSuccess;
}

} // namespace consensus

} // namespace shardora

