#include "consensus/hotstuff/view_block_chain_network.h"
#include <common/global_info.h>
#include <common/utils.h>
#include <consensus/hotstuff/types.h>
#include <dht/dht_function.h>
#include <dht/dht_key.h>
#include <dht/dht_utils.h>
#include <memory>
#include <network/network_utils.h>
#include <network/universal_manager.h>
#include <transport/tcp_transport.h>
#include "network/route.h"
#include "protos/view_block.pb.h"

namespace shardora {
namespace consensus {

ViewBlockChainNetwork::ViewBlockChainNetwork(FetchCallbackFn* fetch_cb_) {
    fetch_callback_fn_ = fetch_cb_;
    // start consumeloop thread
    network::Route::Instance()->RegisterMessage(common::kViewBlockMessage,
        std::bind(&ViewBlockChainNetwork::HandleMessage, this, std::placeholders::_1));
    tick_.CutOff(100000lu, std::bind(&ViewBlockChainNetwork::ConsensusTimerMessage, this));
}

ViewBlockChainNetwork::~ViewBlockChainNetwork() {}

// 同步获取区块
Status ViewBlockChainNetwork::Fetch(const HashStr& view_block_hash, uint32_t pool_idx) {
    return Status::kSuccess;
}

Status ViewBlockChainNetwork::AsyncFetch(const HashStr& view_block_hash, uint32_t pool_idx) {
    auto block_item = std::make_shared<ViewBlockItem>(view_block_hash, pool_idx);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    input_queues_[thread_idx].push(block_item);
    return Status::kSuccess;
}

void ViewBlockChainNetwork::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // TODO 放入消息队列，等待消费
    
}

// 批量异步处理，提高 tps
void ViewBlockChainNetwork::ConsensusTimerMessage() {
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

Status ViewBlockChainNetwork::sendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockMessage& view_block_msg) {
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
Status ViewBlockChainNetwork::processRequest() {}

// 处理 response 类型消息
Status ViewBlockChainNetwork::processResponse() {}

} // namespace consensus

} // namespace shardora

