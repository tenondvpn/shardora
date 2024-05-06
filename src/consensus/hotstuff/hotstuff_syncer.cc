#include "consensus/hotstuff/hotstuff_syncer.h"
#include <common/encode.h>
#include <common/global_info.h>
#include <common/log.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/pacemaker.h>
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

HotstuffSyncer::HotstuffSyncer(
        const std::shared_ptr<consensus::HotstuffManager>& h_mgr) :
    hotstuff_mgr_(h_mgr) {
    // start consumeloop thread
    SetOnRecvViewBlockFn(
            std::bind(&HotstuffSyncer::onRecViewBlock,
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));
    network::Route::Instance()->RegisterMessage(common::kHotstuffSyncMessage,
        std::bind(&HotstuffSyncer::HandleMessage, this, std::placeholders::_1));
}

HotstuffSyncer::~HotstuffSyncer() {}

void HotstuffSyncer::Start() {
    tick_.CutOff(kSyncTimerCycleUs, std::bind(&HotstuffSyncer::ConsensusTimerMessage, this));
}

void HotstuffSyncer::Stop() { tick_.Destroy(); }

int HotstuffSyncer::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void HotstuffSyncer::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto header = msg_ptr->header;
    assert(header.type() == common::kHotstuffSyncMessage);
    
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    consume_queues_[thread_idx].push(msg_ptr);
}

// 批量异步处理，提高 tps
void HotstuffSyncer::ConsensusTimerMessage() {
    // TODO 仅共识池节点参与 view_block_chain 的同步
    // SyncChains();
    SyncChainsToNeighbors();
    ConsumeMessages();

    tick_.CutOff(
        kSyncTimerCycleUs,
        std::bind(&HotstuffSyncer::ConsensusTimerMessage, this));    
}

// Gossip chains info to neighbors
Status HotstuffSyncer::SyncChainsToNeighbors() {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        SyncChainToNeighbor(common::GlobalInfo::Instance()->network_id(), pool_idx);
    }
    return Status::kSuccess;
}

void HotstuffSyncer::ConsumeMessages() {
    // Consume Messages
    uint32_t pop_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (pop_count++ < kPopCountMax) {
            transport::MessagePtr msg_ptr = nullptr;
            consume_queues_[thread_idx].pop(&msg_ptr);
            if (msg_ptr == nullptr) {
                break;
            }
            if (msg_ptr->header.view_block_proto().has_view_block_res()) {
                processResponse(msg_ptr);
            }            
        }   
    }
}

Status HotstuffSyncer::SendMsg(uint32_t network_id, const view_block::protobuf::ViewBlockSyncMessage& view_block_msg) {
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
    msg.set_type(common::kHotstuffSyncMessage);
    *msg.mutable_view_block_proto() = view_block_msg;
    
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);    
    return Status::kSuccess;
}

Status HotstuffSyncer::SyncChainToNeighbor(
        const uint32_t& network_id,
        const uint32_t& pool_idx) {
    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockSyncMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    
    res_view_block_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    auto view_block_res = res_view_block_msg.mutable_view_block_res();
    
    view_block_res->set_network_id(network_id);
    view_block_res->set_pool_idx(pool_idx);
    view_block_res->set_high_qc_str(pacemaker(pool_idx)->HighQC()->Serialize());
    view_block_res->set_high_tc_str(pacemaker(pool_idx)->HighTC()->Serialize());

    auto chain = view_block_chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    std::vector<std::shared_ptr<ViewBlock>> all;
    // 将所有的块同步过去（即最后一个 committed block 及其后续分支
    chain->GetAll(all);
    if (all.size() <= 0 || all.size() > kMaxSyncBlockNum) {
        return Status::kError;
    }

    for (auto& view_block : all) {
        // 仅同步已经有 qc 的 view_block
        auto view_block_qc = chain->GetQcOf(view_block);
        if (!view_block_qc) {
            continue;
        }
        auto view_block_qc_str = view_block_res->add_view_block_qc_strs();
        *view_block_qc_str = view_block_qc->Serialize();
        auto view_block_item = view_block_res->add_view_block_items();
        ViewBlock2Proto(view_block, view_block_item);
    }

    return SendMsg(network_id, res_view_block_msg);
}

// 处理 response 类型消息
Status HotstuffSyncer::processResponse(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_res());
    uint32_t pool_idx = view_block_msg.view_block_res().pool_idx();
    
    processResponseQcTc(pool_idx, view_block_msg.view_block_res());
    return processResponseChain(pool_idx, view_block_msg.view_block_res());
}

Status HotstuffSyncer::processResponseQcTc(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    // 更新 highqc 和 hightc
    auto pm = pacemaker(pool_idx);
    if (!pm) {
        return Status::kError;
    }
    auto highqc = std::make_shared<QC>();
    auto hightc = std::make_shared<TC>();
    highqc->Unserialize(view_block_res.high_qc_str());
    hightc->Unserialize(view_block_res.high_tc_str());

    ZJC_DEBUG("response received qctc pool_idx: %d, tc: %d, qc: %d",
        pool_idx, hightc->view, highqc->view);
    // TODO 验证 qc 和 tc   
    pm->AdvanceView(new_sync_info()->WithQC(highqc)->WithTC(hightc));
    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseChain(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    auto& view_block_items = view_block_res.view_block_items();
    auto& view_block_qc_strs = view_block_res.view_block_qc_strs();

    ZJC_DEBUG("response received pool_idx: %d, view_blocks: %d, qc: %d",
        pool_idx, view_block_items.size(), view_block_qc_strs.size());    
    // 对块数量限制
    // 当出现这么多块，多半是因为共识卡住，不断产生新的无法共识的块，此时同步这些块过来也没有用，早晚会被剪掉
    if (view_block_items.size() > kMaxSyncBlockNum || view_block_items.size() <= 0) {
        return Status::kError;
    }
    if (view_block_items.size() != view_block_qc_strs.size()) {
        return Status::kError;
    }

    auto chain = view_block_chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    // 将 view_block_qc 放入 map 方便查询
    std::unordered_map<HashStr, std::shared_ptr<QC>> view_block_qc_map;
    for (const auto& qc_str : view_block_qc_strs) {
        auto view_block_qc = std::make_shared<QC>();
        if (view_block_qc->Unserialize(qc_str)) {
            view_block_qc_map[view_block_qc->view_block_hash] = view_block_qc;
        }
    }    

    // 将 view_block 放入小根堆排序
    ViewBlockMinHeap min_heap;
    for (auto it = view_block_items.begin(); it != view_block_items.end(); it++) {
        auto view_block = std::make_shared<ViewBlock>();
        Status s = Proto2ViewBlock(*it, view_block);
        if (s != Status::kSuccess) {
            return s;
        }

        // 验证 view_block 的 qc 是否合法
        auto qc_it = view_block_qc_map.find(view_block->hash);
        if (qc_it == view_block_qc_map.end()) {
            continue;
        }
        auto view_block_qc = qc_it->second;
        if (crypto(pool_idx)->VerifyQC(view_block_qc, view_block->ElectHeight()) != Status::kSuccess) {
            continue;
        }

        min_heap.push(view_block);        
    }

    if (min_heap.empty()) {
        return Status::kSuccess;
    }

    // 构造一条临时链，并入 original chain
    auto tmp_chain = std::make_shared<ViewBlockChain>();
    while (!min_heap.empty()) {
        auto view_block = min_heap.top();
        min_heap.pop();

        tmp_chain->Store(view_block);
    }

    ZJC_DEBUG("Sync blocks to chain, pool_idx: %d, view_blocks: %d",
        pool_idx, view_block_items.size());

    if (!tmp_chain->IsValid()) {
        ZJC_ERROR("pool: %d, synced chain is invalid", pool_idx);
        return Status::kSuccess;
    }

    return MergeChain(pool_idx, chain, tmp_chain);    
}

Status HotstuffSyncer::MergeChain(
        const uint32_t& pool_idx,
        std::shared_ptr<ViewBlockChain>& ori_chain,
        const std::shared_ptr<ViewBlockChain>& sync_chain) {
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

        for (const auto& sync_block : sync_all_blocks) {
            if (sync_block->view < cross_block->view) {
                continue;
            }
            if (ori_chain->Has(sync_block->hash)) {
                continue;
            }
            Status s = on_recv_vb_fn_(pool_idx, ori_chain, sync_block);
            if (s != Status::kSuccess) {
                continue;
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
    for (const auto& sync_block : sync_all_blocks) {
        // 逐个处理同步来的 view_block
        Status s = on_recv_vb_fn_(pool_idx, ori_chain, sync_block);
        if (s != Status::kSuccess) {
            continue;
        }
    }
    return Status::kSuccess;
}

// 只要 QC 验证成功，无需验证其他，直接接受该 ViewBlock
Status HotstuffSyncer::onRecViewBlock(
        const uint32_t& pool_idx,
        const std::shared_ptr<ViewBlockChain>& ori_chain,
        const std::shared_ptr<ViewBlock>& view_block) {
    auto hotstuff = hotstuff_mgr_->hotstuff(pool_idx);
    if (!hotstuff) {
        return Status::kError;
    }
    Status s = Status::kSuccess;    
    // 验证交易
    auto accep = hotstuff->acceptor();
    if (!accep) {
        return Status::kError;
    }    
    s = accep->AcceptSync(view_block->block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d sync accept failed", pool_idx);
        return s;
    }
    
    // 2. 视图切换
    hotstuff->pacemaker()->AdvanceView(new_sync_info()->WithQC(view_block->qc));
    
    // 3. 尝试 commit
    auto view_block_to_commit = hotstuff->CheckCommit(view_block);
    if (view_block_to_commit) {
        s = hotstuff->Commit(view_block_to_commit);
        if (s != Status::kSuccess) {
            ZJC_ERROR("pool: %d sync commit failed", pool_idx);
            return s;
        }
    }

    // 4. 保存 view_block
    return hotstuff->view_block_chain()->Store(view_block);
}

} // namespace consensus

} // namespace shardora

