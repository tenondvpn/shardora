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
#include "protos/prefix_db.h"
#include <protos/block.pb.h>
#include <transport/processor.h>
#include <transport/tcp_transport.h>
#include <transport/transport_utils.h>
#include "network/route.h"
#include "protos/view_block.pb.h"
#include "consensus/hotstuff/view_block_chain.h"

/*
TODO: 同步模块目前会将本地内存中整条区块链发送给落后节点，（大约4～8）个块
因此当块本身较大时（如打包了 8192 比交易），会接触到 tcp
缓冲上限，从而无法完成同步，因此需要改造。
目前临时调整块打包交易上限为 4096，以便测试
*/

namespace shardora {

namespace hotstuff {

HotstuffSyncer::HotstuffSyncer(
        const std::shared_ptr<consensus::HotstuffManager>& h_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<block::AccountManager> account_mgr) :
    hotstuff_mgr_(h_mgr), db_(db), kv_sync_(kv_sync), account_mgr_(account_mgr) {
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
    // TODO: test
    if (!running_) {
        return;
    }

    HandleSyncedBlocks();
    // TODO 仅共识池节点参与 view_block_chain 的同步
    SyncAllPools(); // xufeisoflyishere1
    ConsumeMessages();
}

void HotstuffSyncer::SyncPool(const uint32_t& pool_idx, const int32_t& node_num) {
    // TODO(HT): test
    auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
    auto req = vb_msg.mutable_view_block_req();
    req->set_pool_idx(pool_idx);
    req->set_network_id(common::GlobalInfo::Instance()->network_id());
    req->set_high_qc_view(pacemaker(pool_idx)->HighQC()->view());
    req->set_high_tc_view(pacemaker(pool_idx)->HighTC()->view());

    View max_view = 0;
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    view_block_chain(pool_idx)->GetAllVerified(view_blocks);
    // 发送所有有 qc 的 ViewBlock 的 hash 给目标节点
    for (const auto& vb : view_blocks) {
        max_view = vb->view() > max_view ? vb->view() : max_view;
        // 发送本地区块链，只发送 hash 和 parent_hash
        auto vb_item = req->add_view_blocks();
        vb_item->set_hash(vb->hash());
        vb_item->set_parent_hash(vb->parent_hash());
    }
    req->set_max_view(max_view);
    auto latest_committed_block = view_block_chain(pool_idx)->LatestCommittedBlock();
    if (latest_committed_block) {
        req->set_latest_committed_block_hash(latest_committed_block->hash());
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
                SyncPool(pool_idx, 1);
                last_timers_us_[pool_idx] = common::TimeUtils::TimestampUs();
            }            
        }
    }
}

void HotstuffSyncer::HandleSyncedBlocks() {
    auto& block_queue = kv_sync_->vblock_queue();
    std::shared_ptr<view_block::protobuf::ViewBlockItem> pb_vblock = nullptr;
    while (block_queue.pop(&pb_vblock)) {
        if (pb_vblock) {
            if (!pb_vblock->has_self_commit_qc()) {
                continue;
            }
            hotstuff_mgr_->hotstuff(pb_vblock->pool_index())->HandleSyncedViewBlock(
                    pb_vblock);
        }   
    }    
    return;
}

Status HotstuffSyncer::Broadcast(const view_block::protobuf::ViewBlockSyncMessage& view_block_msg) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffSyncMessage);
    header.set_hop_count(0);
    header.mutable_view_block_proto()->CopyFrom(view_block_msg);
    if (!header.has_broadcast()) {
        auto broadcast = header.mutable_broadcast();
    }
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    network::Route::Instance()->Send(msg_ptr);
    return Status::kSuccess;
}

Status HotstuffSyncer::SendRequest(uint32_t network_id, view_block::protobuf::ViewBlockSyncMessage& view_block_msg, int32_t node_num) {
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
    view_block_msg.set_src_ip(common::GlobalInfo::Instance()->config_local_ip());
    view_block_msg.set_src_port(common::GlobalInfo::Instance()->config_local_port());
    
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
    bool shouldSyncLatestCommittedBlock = false;
    HashStr src_latest_committed_block_hash = "";
    
    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockSyncMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    
    res_view_block_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    auto view_block_res = res_view_block_msg.mutable_view_block_res();

    uint32_t network_id = view_block_msg.view_block_req().network_id();
    if (common::GlobalInfo::Instance()->network_id() != network_id) {
        return Status::kError;
    }
    
    uint32_t pool_idx = view_block_msg.view_block_req().pool_idx();
    View src_high_qc_view = view_block_msg.view_block_req().high_qc_view();
    View src_high_tc_view = view_block_msg.view_block_req().high_tc_view();
    View src_max_view = view_block_msg.view_block_req().max_view();
    if (view_block_msg.view_block_req().has_latest_committed_block_hash()) {
        src_latest_committed_block_hash = view_block_msg.view_block_req().latest_committed_block_hash();
    }
    
    // 将 src 节点的 view_block_hashes 放入一个 set
    auto& src_view_block_items = view_block_msg.view_block_req().view_blocks();
    std::unordered_set<HashStr> src_view_block_hash_set;
    for (const auto& src_vb_item : src_view_block_items) {
        src_view_block_hash_set.insert(src_vb_item.hash());
    }
    
    view_block_res->set_network_id(network_id);
    view_block_res->set_pool_idx(pool_idx);

    // src 节点的 highqc 或 hightc 落后，尝试同步
    if (pacemaker(pool_idx)->HighQC()->view() > src_high_qc_view) {
        shouldSyncQC = true;
    }

    if (pacemaker(pool_idx)->HighTC()->view() > src_high_tc_view) {
        shouldSyncTC = true;
    }

    auto latest_committed_block = view_block_chain(pool_idx)->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->hash() != src_latest_committed_block_hash) {
        shouldSyncLatestCommittedBlock = true;
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

    // 找到相交的最后一个 view_block
    View max_view = 0;
    std::shared_ptr<ViewBlock> cross_vb = nullptr;
    for (const auto& view_block: all) {
        auto cross_it = src_view_block_hash_set.find(view_block->hash());
        if (cross_it != src_view_block_hash_set.end()) {
            if (!cross_vb || cross_vb->view() < view_block->view()) {
                cross_vb = view_block;
            }            
        }
    }

    std::vector<std::shared_ptr<ViewBlock>> vb_to_sync; // 要同步的块
    // 存在交点时，仅同步交点之后的
    if (cross_vb) {
        vb_to_sync.clear();
        chain->GetRecursiveChildren(cross_vb->hash(), vb_to_sync);
        // cross_vb 也同步回去，保证是一条合法的链
        vb_to_sync.push_back(cross_vb);
    } else {
        vb_to_sync = all;
    }

    for (const auto& view_block : vb_to_sync) {
        auto view_block_qc = hotstuff_mgr_->hotstuff(pool_idx)->GetQcOf(view_block);
        if (!view_block_qc) {
            continue;
        }

        auto add_qc = view_block_res->add_view_block_qcs();
        *add_qc = *view_block_qc;
        auto add_view_block_item = view_block_res->add_view_block_items();
        *add_view_block_item = *view_block;
        max_view = view_block->view() > max_view ? view_block->view() : max_view;
    }

    if (view_block_res->view_block_items_size() > 0) {
        shouldSyncChain = true;
    }

    // 若只有 cross_vb 一个块，则不同步（因为 cross_vb 已经有了）
    if (view_block_res->view_block_items_size() == 1 &&
        cross_vb &&
        view_block_res->view_block_items(0).hash() == cross_vb->hash()) {
        shouldSyncChain = false;
    }

    // 若本地 view_block_chain 的最大 view < src 节点，则不同步
    if (max_view < src_max_view) {
        shouldSyncChain = false;
    }

    // 不发送消息
    if (!shouldSyncChain && !shouldSyncQC && !shouldSyncTC) {
        return Status::kSuccess;
    }

    if (!shouldSyncChain) {
        view_block_res->clear_view_block_items();
        view_block_res->clear_view_block_qcs();
    }

    if (shouldSyncTC) {
        *view_block_res->mutable_high_tc() = *pacemaker(pool_idx)->HighTC();
    }

    if (shouldSyncQC) {
        *view_block_res->mutable_high_qc() = *pacemaker(pool_idx)->HighQC();
    }

    // TODO LatestCommittedblock 如果限于 Propose 消息使得 Leader 变更，则会导致 replica VerifyLeader 失败，从而无法继续参与共识
    // 理论上只需要同步 Chain 改变 Leader 即可，但由于用了 ExpectedLeader，其实可以解决这个问题，因此这里先加上
    // 另外由于单线程，当 Propose 设置了 LatestCommittedBlock 之后肯定会继续更改 Chain，所以应该不存在只同步 LatestCommitBlock 而不同步新 Chain 的情况，因此实测下来加上更稳定
    if (shouldSyncLatestCommittedBlock) {
        // auto latest_committed_qc = view_block_chain(pool_idx)->GetCommitQcFromDb(latest_committed_block->hash());
        *view_block_res->mutable_latest_committed_block() = *latest_committed_block;
    }
    
    return ReplyMsg(network_id, res_view_block_msg, msg_ptr);
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
        }
    }
}

Status HotstuffSyncer::ReplyMsg(
        uint32_t network_id,
        const view_block::protobuf::ViewBlockSyncMessage& view_block_msg,
        const transport::MessagePtr& msg_ptr) {
    transport::protobuf::Header msg;
    msg.set_src_sharding_id(network_id);
    dht::DhtKeyManager dht_key(network_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kHotstuffSyncMessage);
    msg.mutable_view_block_proto()->CopyFrom(view_block_msg);
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto ip = msg_ptr->header.view_block_proto().src_ip();
    auto port = msg_ptr->header.view_block_proto().src_port();
    ZJC_DEBUG("pool: %d, network: %lu, ip: %s, port: %d, with_query_hash: %d, hash64: %lu",
        view_block_msg.view_block_res().pool_idx(), network_id,
        ip.c_str(),
        port,
        msg.view_block_proto().view_block_res().has_query_hash(),
        msg.hash64());
    transport::TcpTransport::Instance()->Send(msg_ptr->conn.get(), msg);
    return Status::kSuccess; 
}

// 处理 response 类型消息
Status HotstuffSyncer::processResponse(const transport::MessagePtr& msg_ptr) {
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_res());
    uint32_t network_id = view_block_msg.view_block_res().network_id();
    uint32_t pool_idx = view_block_msg.view_block_res().pool_idx();
    
    if (common::GlobalInfo::Instance()->network_id() != network_id) {
        return Status::kError;
    }
    
    processResponseQcTc(pool_idx, view_block_msg.view_block_res()); // xufeisoflyishere2
    processResponseLatestCommittedBlock(pool_idx, view_block_msg.view_block_res());
    Status s = processResponseChain(pool_idx, view_block_msg.view_block_res());
    if (s != Status::kSuccess) {
        return s;
    }
    // 同步之后尝试消费之前消费失败的 Block
    hotstuff_mgr_->hotstuff(pool_idx)->TryWaitingProposeMsgs();
    return Status::kSuccess;
}

// TODO 临时屏蔽，反正也不走这部分逻辑
Status HotstuffSyncer::processResponseQcTc(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    // 更新 highqc 和 hightc
    if (!view_block_res.has_high_qc() && !view_block_res.has_high_tc()) {
        return Status::kSuccess;
    }
    auto pm = pacemaker(pool_idx);
    if (!pm) {
        return Status::kError;
    }
    auto highqc = std::make_shared<QC>(view_block_res.high_qc());
    auto hightc = std::make_shared<TC>(view_block_res.high_tc());

    if ((highqc->network_id() > 0 && highqc->network_id() != common::GlobalInfo::Instance()->network_id()) ||
            (hightc->network_id() > 0 && hightc->network_id() != common::GlobalInfo::Instance()->network_id())) {
        ZJC_DEBUG("error network id hight qc: %u, hight tc: %u, local: %u",
            highqc->network_id(), hightc->network_id(), 
            common::GlobalInfo::Instance()->network_id());
        assert(false);
        return Status::kError;
    }

    // 设置 view_block 的 qc
    view_block_chain(pool_idx)->SetQcOf(highqc->view_block_hash(), highqc);

    ZJC_DEBUG("response received qctc pool_idx: %d, tc: %d, qc: %d",
        pool_idx, hightc->view(), highqc->view());

    // TODO 验证 qc 和 tc
    pm->AdvanceView(new_sync_info()->WithQC(highqc)->WithTC(hightc));
    // 尝试做 commit
    transport::MessagePtr msg_ptr;
    hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(msg_ptr, *highqc);

    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseLatestCommittedBlock(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    if (!view_block_res.has_latest_committed_block()) {
        return Status::kError;
    }
    
    auto& pb_latest_committed_block = view_block_res.latest_committed_block();
    // 可能已经更新，无需同步
    auto cur_latest_committed_block = view_block_chain(pool_idx)->LatestCommittedBlock();
    if (cur_latest_committed_block &&
            cur_latest_committed_block->view() >= pb_latest_committed_block.view()) {
        return Status::kSuccess;
    }    

    ZJC_DEBUG("pool: %d sync latest committed block: %lu", pool_idx, pb_latest_committed_block.view());
    // 执行 latest committed block
    auto hf = hotstuff_mgr_->hotstuff(pb_latest_committed_block.pool_index());
    auto latest_vblock = std::make_shared<ViewBlock>(pb_latest_committed_block);
    hf->HandleSyncedViewBlock(latest_vblock);
    return Status::kSuccess;
}


Status HotstuffSyncer::processResponseChain(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    auto& view_block_items = view_block_res.view_block_items();
    auto& view_block_qcs = view_block_res.view_block_qcs();
    
    if (view_block_items.size() <= 0) {
        return Status::kSuccess;
    }
    // 对块数量限制
    // 当出现这么多块，多半是因为共识卡住，不断产生新的无法共识的块，此时同步这些块过来也没有用，早晚会被剪掉
    if (view_block_items.size() > kMaxSyncBlockNum) {
        ZJC_ERROR("pool: %d, view block: %lu exceeds max limit", pool_idx, view_block_items.size());
        return Status::kError;
    }

    ZJC_DEBUG("response received pool_idx: %d, view_blocks: %d",
        pool_idx, view_block_items.size());
    
    auto chain = view_block_chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    // 将 view_block_qc 放入 map 方便查询
    std::unordered_map<HashStr, QC> view_block_qc_map;
    for (const auto& qc : view_block_qcs) {
        if (IsQcTcValid(qc)) {
            view_block_qc_map[qc.view_block_hash()] = qc;
        }
    }        

    // 将 view_block 放入小根堆排序
    ViewBlockMinHeap min_heap;
    std::shared_ptr<QC> high_commit_qc = nullptr;
    for (auto it = view_block_items.begin(); it != view_block_items.end(); it++) {
        auto& view_block = it;

        // 验证 view_block 的 qc 是否合法
        auto qc_it = view_block_qc_map.find(view_block->hash());
        if (qc_it == view_block_qc_map.end()) {
            continue;
        }

        auto& view_block_qc = qc_it->second;
        if (!IsQcTcValid(view_block_qc)) {            
            continue;
        }
        
        // 如果本地有该 view_block_qc 对应的 view_block，则不用验证 qc 了并且跳过该块，节省 CPU
        if (!chain->Has(view_block_qc.view_block_hash()) && 
                crypto(pool_idx)->VerifyQC(
                    common::GlobalInfo::Instance()->network_id(), 
                    view_block_qc) != Status::kSuccess) {
            continue;
        }
        
        // 记录同步链中最高的 Qc，用于 commit
        if (!high_commit_qc || high_commit_qc->view() < view_block_qc.view()) {
            high_commit_qc = std::make_shared<QC>(view_block_qc);
        }
        
        min_heap.push(std::make_shared<ViewBlock>(*view_block));        
    }

    if (min_heap.empty()) {
        return Status::kSuccess;
    }

    // 构造一条临时链，并入 original chain
    auto tmp_chain = std::make_shared<ViewBlockChain>(pool_idx, db_, account_mgr_);
    while (!min_heap.empty()) {
        auto view_block = min_heap.top();
        min_heap.pop();
        // TODO: check valid
        tmp_chain->Store(view_block, true, nullptr, nullptr);
    }
    
    ZJC_INFO("Sync blocks to chain, pool_idx: %d, view_blocks: %d, syncchain: %s, orichain: %s",
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
        cross_block = sync_chain->Get((*it)->hash());
        if (cross_block) {
            break;
        }
    }

    // 存在交点，则可以将 sync_chain 依次全部加入 ori_chain
    // 重复的块不需要重复添加，但有可能携带 commit qc，要尝试 TryCommit
    // 两条链不存在交点也无法连接，则替换为 max_view 更大的链
    if (!cross_block) {
        // 两条链不存在交点也无法连接，则替换为 max_view 更大的链
        auto ori_max_height = ori_chain->GetMaxHeight();
        auto sync_max_height = sync_chain->GetMaxHeight();
        if (ori_max_height >= sync_max_height) {
            return Status::kSuccess;
        }

        ori_chain->Clear();        
    } 

    std::vector<std::shared_ptr<ViewBlock>> sync_all_blocks;
    sync_chain->GetOrderedAll(sync_all_blocks);
    for (const auto& sync_block : sync_all_blocks) {
        // 如果存在交点，则交点之前的块不考虑
        if (cross_block && sync_block->view() < cross_block->view()) {
            continue;
        }
        Status s = onRecViewBlock(pool_idx, ori_chain, *sync_block); // xufeisoflyishere3
        if (s != Status::kSuccess) {
            ZJC_ERROR("pool: %d, merge chain block: %lu failed, s: %d",
                pool_idx, sync_block->view(), s);
            continue;
        }
    }

    // 单独对 high_commit_qc 提交
    // 保证落后节点虽然没有最新的提案，但是有最新的 qc，并且 leader 一致
    transport::MessagePtr msg_ptr;
    hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(msg_ptr, *high_commit_qc);
    pacemaker(pool_idx)->AdvanceView(new_sync_info()->WithQC(high_commit_qc));
    return Status::kSuccess;
}

// 只要 QC 验证成功，无需验证其他，直接接受该 ViewBlock
Status HotstuffSyncer::onRecViewBlock(
        const uint32_t& pool_idx,
        const std::shared_ptr<ViewBlockChain>& ori_chain,
        const ViewBlock& view_block) {
    auto hotstuff = hotstuff_mgr_->hotstuff(pool_idx);
    if (!hotstuff) {
        return Status::kError;
    }

    Status s = Status::kSuccess;    
    auto view_block_ptr = std::make_shared<ViewBlock>(view_block);

    hotstuff->pacemaker()->AdvanceView(new_sync_info()->WithQC(std::make_shared<QC>(view_block_ptr->qc())));
    transport::MessagePtr msg_ptr;
    hotstuff->TryCommit(msg_ptr, view_block.qc());

    // 如果已经有此块，则直接返回
    if (view_block_chain(pool_idx)->Has(view_block.hash())) {
        return Status::kSuccess;
    }

    // 验证交易
    auto accep = hotstuff->acceptor();
    if (!accep) {
     
        return Status::kError;
    }
    s = accep->AcceptSync(*view_block_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d sync accept failed", pool_idx);
        return s;
    }    

    // 4. 保存 view_block
    // TODO: check valid
    s = hotstuff->view_block_chain()->Store(view_block_ptr, true, nullptr, nullptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pool: %d store view block failed, hash: %s, view: %lu, cur chain: %s", pool_idx,
            common::Encode::HexEncode(view_block.hash()).c_str(), 
            view_block.view(), 
            view_block_chain(pool_idx)->String().c_str());
        return s;
    }

    // 标记交易占用
    hotstuff->acceptor()->MarkBlockTxsAsUsed(view_block.block_info());
    return Status::kSuccess;
}

} // namespace consensus

} // namespace shardora

