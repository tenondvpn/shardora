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
#include <protos/block.pb.h>
#include <transport/processor.h>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "network/route.h"
#include "protos/view_block.pb.h"
#include "consensus/hotstuff/view_block_chain.h"

namespace shardora {

namespace hotstuff {

HotstuffSyncer::HotstuffSyncer(
        const std::shared_ptr<consensus::HotstuffManager>& h_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) :
    hotstuff_mgr_(h_mgr), db_(db), kv_sync_(kv_sync) {
    // start consumeloop thread
    SetOnRecvViewBlockFn(
            std::bind(&HotstuffSyncer::onRecViewBlock,
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));

    // 设置 pacemaker 中的同步函数
    hotstuff_mgr_->SetSyncPoolFn(
            std::bind(&HotstuffSyncer::SyncPool,
                    this, std::placeholders::_1, std::placeholders::_2));
    hotstuff_mgr_->SetSyncViewBlockFn(
            std::bind(&HotstuffSyncer::SyncViewBlock,
                this, std::placeholders::_1, std::placeholders::_2));    

    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        last_timers_us_[i] = common::TimeUtils::TimestampUs();
    }
    
    network::Route::Instance()->RegisterMessage(common::kHotstuffSyncMessage,
        std::bind(&HotstuffSyncer::HandleMessage, this, std::placeholders::_1));
    transport::Processor::Instance()->RegisterProcessor(
        common::kHotstuffSyncTimerMessage,
        std::bind(&HotstuffSyncer::ConsensusTimerMessage, this, std::placeholders::_1));    
}

HotstuffSyncer::~HotstuffSyncer() {}

void HotstuffSyncer::Start() {
    running_ = true;
}

void HotstuffSyncer::Stop() {
    running_ = false;
}

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
void HotstuffSyncer::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    // TODO 仅共识池节点参与 view_block_chain 的同步
    SyncAllPools();
    ConsumeMessages();
    HandleSyncedBlocks();
}

void HotstuffSyncer::SyncPool(const uint32_t& pool_idx, const int32_t& node_num) {
    // TODO(HT): test
    auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
    auto req = vb_msg.mutable_view_block_req();
    req->set_pool_idx(pool_idx);
    req->set_network_id(common::GlobalInfo::Instance()->network_id());
    req->set_high_qc_view(pacemaker(pool_idx)->HighQC()->view);
    req->set_high_tc_view(pacemaker(pool_idx)->HighTC()->view);

    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    view_block_chain(pool_idx)->GetAll(view_blocks);
    // 发送所有 ViewBlock 的 hash 给目标节点
    for (const auto& vb : view_blocks) {
        auto& vb_hash = *(req->add_view_block_hashes());
        vb_hash = vb->hash;
    }
        
    vb_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    SendRequest(common::GlobalInfo::Instance()->network_id(), vb_msg, node_num);
}

void HotstuffSyncer::SyncAllPools() {
    if (!running_) {
        return;
    }
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    auto now_us = common::TimeUtils::TimestampUs();
    
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        if (now_us - last_timers_us_[pool_idx] >= SyncTimerCycleUs(pool_idx)) {
            if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_index) {
                // ZJC_DEBUG("pool: %d, sync pool, timeout_duration: %lu ms",
                //     pool_idx, SyncTimerCycleUs(pool_idx)/1000);
                SyncPool(pool_idx, 1);
                last_timers_us_[pool_idx] = common::TimeUtils::TimestampUs();
            }            
        }
    }
}

void HotstuffSyncer::SyncViewBlock(const uint32_t& pool_idx, const HashStr& hash) {
    auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
    auto req = vb_msg.mutable_single_req();
    req->set_pool_idx(pool_idx);
    req->set_network_id(common::GlobalInfo::Instance()->network_id());
    req->set_query_hash(hash);
        
    vb_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    // 询问所有邻居节点
    SendRequest(common::GlobalInfo::Instance()->network_id(), vb_msg, 1);
    return;
}

void HotstuffSyncer::HandleSyncedBlocks() {
    auto& block_queue = kv_sync_->vblock_queue();
    std::shared_ptr<view_block::protobuf::ViewBlockItem> pb_vblock = nullptr;
    while (block_queue.pop(&pb_vblock)) {
        if (pb_vblock) {
            auto block = std::make_shared<block::protobuf::Block>(pb_vblock->block_info());
            auto vblock = std::make_shared<ViewBlock>();
            Status s = Proto2ViewBlock(*pb_vblock.get(), vblock);
            if (s != Status::kSuccess) {
                continue;
            }
            auto self_commit_qc = std::make_shared<QC>();
            if (!self_commit_qc->Unserialize(pb_vblock->self_commit_qc_str())) {
                continue;
            }
            hotstuff_mgr_->hotstuff(pb_vblock->block_info().pool_index())->HandleSyncedViewBlock(
                    vblock, self_commit_qc);
        }
        
    }    
    return;
}

Status HotstuffSyncer::SendRequest(uint32_t network_id, const view_block::protobuf::ViewBlockSyncMessage& view_block_msg, int32_t node_num) {
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
    // dht::NodePtr node = nodes[rand() % nodes.size()];
    if (node_num == -1) {
        node_num = nodes.size();
    }
    
    if (node_num > int32_t(nodes.size())) {
        return Status::kError;
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(nodes.begin(), nodes.end(), g);
    std::vector<dht::NodePtr> selectedNodes(nodes.begin(), nodes.begin() + node_num);    

    transport::protobuf::Header msg;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(network_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kHotstuffSyncMessage);
    *msg.mutable_view_block_proto() = view_block_msg;
    
    transport::TcpTransport::Instance()->SetMessageHash(msg);

    for (const auto& node : selectedNodes) {
        transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);            
    }
    return Status::kSuccess;
}

// 处理 request 类型消息
Status HotstuffSyncer::processRequest(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_req());
    
    bool shouldSyncQC = false;
    bool shouldSyncTC = false;
    bool shouldSyncChain = false; 
    
    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockSyncMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    
    res_view_block_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    auto view_block_res = res_view_block_msg.mutable_view_block_res();

    uint32_t network_id = view_block_msg.view_block_req().network_id();
    uint32_t pool_idx = view_block_msg.view_block_req().pool_idx();
    View src_high_qc_view = view_block_msg.view_block_req().high_qc_view();
    View src_high_tc_view = view_block_msg.view_block_req().high_tc_view();
    // 将 src 节点的 view_block_hashes 放入一个 set
    auto& src_view_block_hashes = view_block_msg.view_block_req().view_block_hashes();
    std::unordered_set<HashStr> src_view_block_hash_set;
    for (const auto& hash : src_view_block_hashes) {
        src_view_block_hash_set.insert(hash);
    }
    
    view_block_res->set_network_id(network_id);
    view_block_res->set_pool_idx(pool_idx);

    // src 节点的 highqc 或 hightc 落后，尝试同步
    if (pacemaker(pool_idx)->HighQC()->view > src_high_qc_view) {
        shouldSyncQC = true;
    }

    if (pacemaker(pool_idx)->HighTC()->view > src_high_tc_view) {
        shouldSyncTC = true;
    }

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

    // 检查本地 ViewBlockChain 中是否存在 src 节点没有的 ViewBlock(需要有 qc)，如果存在则全部同步过去
    // 由于仅检查有 qc 的 view block，因此最新的 view_block 并不会同步（本该如此），但其中的 qc 也不会随着 chain 同步
    // 导致超时 leader 不一致（因为 leader 是跟随 qc 迭代的）
    // 好在这个 qc 会通过 highqc 同步过去，因此接受 highqc 时需要执行 commit 操作，保证 leader 一致    
    for (auto& view_block : all) {
        // 仅同步已经有 qc 的 view_block
        auto view_block_qc = chain->GetQcOf(view_block);
        if (!view_block_qc) {
            if (pacemaker(pool_idx)->HighQC()->view_block_hash == view_block->hash) {
                chain->SetQcOf(view_block, pacemaker(pool_idx)->HighQC());
                view_block_qc = pacemaker(pool_idx)->HighQC();
            } else {
                continue;
            }
        }

        // src 节点没有此 view_block
        if (!shouldSyncChain) {
            auto it = src_view_block_hash_set.find(view_block->hash);
            if (it == src_view_block_hash_set.end()) {
                shouldSyncChain = true;
            }            
        }
        
        auto view_block_qc_str = view_block_res->add_view_block_qc_strs();
        *view_block_qc_str = view_block_qc->Serialize();
        auto view_block_item = view_block_res->add_view_block_items();
        ViewBlock2Proto(view_block, view_block_item);
    }

    // 不发送消息
    if (!shouldSyncChain && !shouldSyncQC && !shouldSyncTC) {
        return Status::kSuccess;
    }

    if (!shouldSyncChain) {
        view_block_res->clear_view_block_qc_strs();
        view_block_res->clear_view_block_items();
    }

    if (shouldSyncTC) {
        view_block_res->set_high_tc_str(pacemaker(pool_idx)->HighTC()->Serialize());
    }

    if (shouldSyncQC) {
        view_block_res->set_high_qc_str(pacemaker(pool_idx)->HighQC()->Serialize());
    }
    
    return SendMsg(network_id, res_view_block_msg);
}

Status HotstuffSyncer::processRequestSingle(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_single_req());

    uint32_t network_id = view_block_msg.single_req().network_id();
    uint32_t pool_idx = view_block_msg.single_req().pool_idx();

    auto chain = view_block_chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    auto query_hash = view_block_msg.single_req().query_hash();

    // 不存在 query_hash 则不同步
    std::shared_ptr<ViewBlock> view_block = nullptr;
    chain->Get(query_hash, view_block);
    if (!view_block || chain->GetQcOf(view_block)) {
        return Status::kNotFound;
    }

    std::vector<std::shared_ptr<ViewBlock>> all;
    chain->GetAll(all);
    if (all.size() <= 0 || all.size() > kMaxSyncBlockNum) {
        return Status::kError;
    }

    // 检查本地 ViewBlockChain 中是否存在 src 节点没有的 ViewBlock，如果存在则全部同步过去
    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockSyncMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    auto view_block_res = res_view_block_msg.mutable_view_block_res();
    for (auto& view_block : all) {
        // 仅同步已经有 qc 的 view_block
        auto view_block_qc = chain->GetQcOf(view_block);
        if (!view_block_qc) {
            if (pacemaker(pool_idx)->HighQC()->view_block_hash == view_block->hash) {
                chain->SetQcOf(view_block, pacemaker(pool_idx)->HighQC());
                view_block_qc = pacemaker(pool_idx)->HighQC();
            } else {
                continue;
            }
        }
        
        auto view_block_qc_str = view_block_res->add_view_block_qc_strs();
        *view_block_qc_str = view_block_qc->Serialize();
        auto view_block_item = view_block_res->add_view_block_items();
        ViewBlock2Proto(view_block, view_block_item);
    }

    view_block_res->set_network_id(network_id);
    view_block_res->set_pool_idx(pool_idx);
    view_block_res->set_query_hash(query_hash); // 用于帮助 src 节点过滤冗余
    res_view_block_msg.set_create_time_us(common::TimeUtils::TimestampUs());

    return SendMsg(network_id, res_view_block_msg);
}

void HotstuffSyncer::ConsumeMessages() {
    // Consume Messages
    uint32_t pop_count = 0;
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
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
        } else if (msg_ptr->header.view_block_proto().has_single_req()) {
            processRequestSingle(msg_ptr);
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

// 处理 response 类型消息
Status HotstuffSyncer::processResponse(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_res());
    uint32_t pool_idx = view_block_msg.view_block_res().pool_idx();

    
    if (view_block_msg.view_block_res().has_query_hash()) {
        // 处理 single query
        // 已经有该 view block 了，直接返回
        if (view_block_chain(pool_idx)->Has(view_block_msg.view_block_res().query_hash())) {
            ZJC_DEBUG("pool: %d, has query hash", pool_idx);
            return Status::kSuccess;
        }
    }
    
    processResponseQcTc(pool_idx, view_block_msg.view_block_res());
    return processResponseChain(pool_idx, view_block_msg.view_block_res());
}

Status HotstuffSyncer::processResponseQcTc(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    // 更新 highqc 和 hightc
    if (!view_block_res.has_high_qc_str() && !view_block_res.has_high_tc_str()) {
        return Status::kSuccess;
    }
    auto pm = pacemaker(pool_idx);
    if (!pm) {
        return Status::kError;
    }
    auto highqc = std::make_shared<QC>();
    auto hightc = std::make_shared<TC>();
    highqc->Unserialize(view_block_res.high_qc_str());
    hightc->Unserialize(view_block_res.high_tc_str());
    // 设置 view_block 的 qc
    view_block_chain(pool_idx)->SetQcOf(highqc->view_block_hash, highqc);

    ZJC_DEBUG("response received qctc pool_idx: %d, tc: %d, qc: %d",
        pool_idx, hightc->view, highqc->view);

    // TODO 验证 qc 和 tc
    pm->AdvanceView(new_sync_info()->WithQC(highqc)->WithTC(hightc));
    // 尝试做 commit
    hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(highqc);
    
    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseChain(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    auto& view_block_items = view_block_res.view_block_items();
    auto& view_block_qc_strs = view_block_res.view_block_qc_strs();

    if (view_block_items.size() <= 0) {
        return Status::kSuccess;
    }
    // 对块数量限制
    // 当出现这么多块，多半是因为共识卡住，不断产生新的无法共识的块，此时同步这些块过来也没有用，早晚会被剪掉
    if (view_block_items.size() > kMaxSyncBlockNum) {
        ZJC_ERROR("pool: %d, view block: %lu exceeds max limit", pool_idx, view_block_items.size());
        return Status::kError;
    }

    
    if (view_block_items.size() != view_block_qc_strs.size()) {
        return Status::kError;
    }

    ZJC_DEBUG("response received pool_idx: %d, view_blocks: %d, qc: %d",
        pool_idx, view_block_items.size(), view_block_qc_strs.size());

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
    std::shared_ptr<QC> high_commit_qc = nullptr;
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
        // 如果本地有该 view_block_qc 对应的 view_block，则不用验证 qc 了并且跳过该块，节省 CPU
        if (!chain->Has(view_block_qc->view_block_hash) &&
            crypto(pool_idx)->VerifyQC(view_block_qc) != Status::kSuccess) {
            continue;
        }
        
        // 记录同步链中最高的 Qc，用于 commit
        if (!high_commit_qc) {
            high_commit_qc = view_block_qc;
        } else if (high_commit_qc->view < view_block_qc->view) {
            high_commit_qc = view_block_qc;
        }
        
        min_heap.push(view_block);        
    }

    if (min_heap.empty()) {
        return Status::kSuccess;
    }

    // 构造一条临时链，并入 original chain
    auto tmp_chain = std::make_shared<ViewBlockChain>(db_);
    while (!min_heap.empty()) {
        auto view_block = min_heap.top();
        min_heap.pop();

        tmp_chain->Store(view_block);
    }
    
    ZJC_DEBUG("Sync blocks to chain, pool_idx: %d, view_blocks: %d, syncchain: %s, orichain: %s",
        pool_idx, view_block_items.size(), tmp_chain->String().c_str(), chain->String().c_str());

    if (!tmp_chain->IsValid()) {
        ZJC_ERROR("pool: %d, synced chain is invalid", pool_idx);
        return Status::kSuccess;
    }

    return MergeChain(pool_idx, chain, tmp_chain, high_commit_qc);    
}

Status HotstuffSyncer::MergeChain(
        const uint32_t& pool_idx,
        std::shared_ptr<ViewBlockChain>& ori_chain,
        const std::shared_ptr<ViewBlockChain>& sync_chain,
        const std::shared_ptr<QC>& high_commit_qc) {
    // 寻找交点
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    ori_chain->GetOrderedAll(view_blocks);
    std::shared_ptr<ViewBlock> cross_block = nullptr;
    for (auto it = view_blocks.rbegin(); it != view_blocks.rend(); it++) {
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
        // 单独对 high_commit_qc 提交
        // 保证落后节点虽然没有最新的提案，但是有最新的 qc，并且 leader 一致
        hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(high_commit_qc);
        pacemaker(pool_idx)->AdvanceView(new_sync_info()->WithQC(high_commit_qc));
        
        return Status::kSuccess;
    }
    
    std::vector<std::shared_ptr<ViewBlock>> sync_all_blocks;
    sync_chain->GetOrderedAll(sync_all_blocks);

    // 两条链不存在交点也无法连接，则替换为 max_view 更大的链
    auto ori_max_height = ori_chain->GetMaxHeight();
    auto sync_max_height = sync_chain->GetMaxHeight();
    if (ori_max_height >= sync_max_height) {
        return Status::kSuccess;
    }

    ori_chain->Clear();
    for (const auto& sync_block : sync_all_blocks) {
        // 逐个处理同步来的 view_block
        Status s = on_recv_vb_fn_(pool_idx, ori_chain, sync_block);
        if (s != Status::kSuccess) {
            continue;
        }
    }

    hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(high_commit_qc);
    pacemaker(pool_idx)->AdvanceView(new_sync_info()->WithQC(high_commit_qc));
    
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
    
    // 2. 视图切换
    hotstuff->pacemaker()->AdvanceView(new_sync_info()->WithQC(view_block->qc));
    
    // 3. 尝试 commit
    // TODO 有更新的 qc
    hotstuff->TryCommit(view_block->qc);

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

    // 4. 保存 view_block
    return hotstuff->view_block_chain()->Store(view_block);
}

} // namespace consensus

} // namespace shardora

