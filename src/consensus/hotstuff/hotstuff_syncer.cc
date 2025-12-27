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

    // Set the synchronization function in pacemaker
    hotstuff_mgr_->SetSyncPoolFn(
            std::bind(&HotstuffSyncer::SyncPool,
                    this, std::placeholders::_1, std::placeholders::_2));
    network::Route::Instance()->RegisterMessage(common::kHotstuffSyncMessage,
        std::bind(&HotstuffSyncer::HandleMessage, this, std::placeholders::_1));
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
    // TODO: test
    return;
    auto header = msg_ptr->header;
    assert(header.type() == common::kHotstuffSyncMessage);
    
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();    
    consume_queues_[thread_idx].push(msg_ptr);
}

// 批量异步处理，提高 tps
// Batch asynchronous processing to improve tps
void HotstuffSyncer::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    // TODO: test
    if (!running_) {
        return;
    }
    // TODO Only consensus pool nodes participate in the synchronization of view_block_chain
    // SyncAllPools();
    // ConsumeMessages();
    HandleSyncedBlocks();
}

void HotstuffSyncer::SyncPool(const uint32_t& pool_idx, const int32_t& node_num) {
    auto latest_view_block = view_block_chain(pool_idx)->HighViewBlock();
    if (!latest_view_block) {
        return;
    }

    // TODO(HT): test
    auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
    auto req = vb_msg.mutable_view_block_req();
    req->set_pool_idx(pool_idx);
    req->set_network_id(common::GlobalInfo::Instance()->network_id());
    req->set_high_qc_view(latest_view_block->qc().view());
    req->set_high_tc_view(pacemaker(pool_idx)->HighTC()->view());

    View max_view = 0;
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    view_block_chain(pool_idx)->GetAllVerified(view_blocks);
    // Send the hash of all ViewBlocks with qc to the target node
    for (const auto& vb : view_blocks) {
        // auto& vb_hash = *(req->add_view_block_hashes());
        // vb_hash = vb->hash;
        max_view = vb->qc().view() > max_view ? vb->qc().view() : max_view;
        // Send the local blockchain, only send hash and parent_hash
        auto vb_item = req->add_view_blocks();
        vb_item->mutable_qc()->set_view_block_hash(vb->qc().view_block_hash());
        SHARDORA_DEBUG("success set view block hash: %s, parent: %s, %u_%u_%lu",
            common::Encode::HexEncode(vb_item->qc().view_block_hash()).c_str(),
            common::Encode::HexEncode(vb_item->parent_hash()).c_str(),
            vb_item->qc().network_id(),
            vb_item->qc().pool_index(),
            vb_item->qc().view());
        // TODO: check valid
        // auto prefix_db = std::make_shared<protos::PrefixDb>(db_);
        // assert(!prefix_db->BlockExists(vb_item->qc().view_block_hash()));
        vb_item->set_parent_hash(vb->parent_hash());
    }
    req->set_max_view(max_view);
    auto latest_committed_block = view_block_chain(pool_idx)->LatestCommittedBlock();
    if (latest_committed_block) {
        req->set_latest_committed_block_hash(latest_committed_block->qc().view_block_hash());
    }
        
    vb_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    SendRequest(common::GlobalInfo::Instance()->network_id(), vb_msg, node_num);
}

void HotstuffSyncer::SyncAllPools() {
    assert(false);
}

// No use, about to deprecate.
void HotstuffSyncer::SyncViewBlock(const uint32_t& pool_idx, const HashStr& hash) {
    assert(false);
    auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
    auto req = vb_msg.mutable_single_req();
    req->set_pool_idx(pool_idx);
    req->set_network_id(common::GlobalInfo::Instance()->network_id());
    req->set_query_hash(hash);
        
    vb_msg.set_create_time_us(common::TimeUtils::TimestampUs());
    // Ask all neighbor nodes
    SendRequest(common::GlobalInfo::Instance()->network_id(), vb_msg, 1);
    return;
}

void HotstuffSyncer::HandleSyncedBlocks() {
    // auto& block_queue = kv_sync_->vblock_queue();
    // std::shared_ptr<view_block::protobuf::ViewBlockItem> pb_vblock = nullptr;
    // while (block_queue.pop(&pb_vblock)) {
    //     if (pb_vblock) {
    //         hotstuff_mgr_->hotstuff(pb_vblock->qc().pool_index())->HandleSyncedViewBlock(
    //                 pb_vblock);
    //     }
    // }    
}

Status HotstuffSyncer::Broadcast(const view_block::protobuf::ViewBlockSyncMessage& view_block_msg) {
    assert(false);
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
    // Only consensus pool nodes can synchronize ViewBlock
    if (network_id >= network::kConsensusWaitingShardBeginNetworkId) {
        return Status::kError;
    }
    // Get neighbor nodes
    std::vector<dht::NodePtr> nodes;
    SHARDORA_DEBUG("now get universal dht 0");
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

// Process request type messages
Status HotstuffSyncer::processRequest(const transport::MessagePtr& msg_ptr) {
    SHARDORA_DEBUG("handle message hash: %lu", msg_ptr->header.hash64());
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
    
    // Put the view_block_hashes of the src node into a set
    auto& src_view_block_items = view_block_msg.view_block_req().view_blocks();
    std::unordered_set<HashStr> src_view_block_hash_set;
    for (const auto& src_vb_item : src_view_block_items) {
        src_view_block_hash_set.insert(src_vb_item.qc().view_block_hash());
    }
    
    view_block_res->set_network_id(network_id);
    view_block_res->set_pool_idx(pool_idx);

    // The highqc or hightc of the src node is lagging, try to synchronize
    if (view_block_chain(pool_idx)->HighViewBlock()->qc().view() > src_high_qc_view) {
        shouldSyncQC = true;
    }

    if (pacemaker(pool_idx)->HighTC()->view() > src_high_tc_view) {
        shouldSyncTC = true;
    }

    auto latest_committed_block = view_block_chain(pool_idx)->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->qc().view_block_hash() != src_latest_committed_block_hash) {
        shouldSyncLatestCommittedBlock = true;
    }

    auto chain = view_block_chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    std::vector<std::shared_ptr<ViewBlock>> all;
    // Synchronize all blocks (that is, the last committed block and its subsequent branches)
    chain->GetAll(all);
    if (all.size() <= 0 || all.size() > kMaxSyncBlockNum) {
        return Status::kError;
    }

    // Check if there is a ViewBlock in the local ViewBlockChain that the src node does not have (requires qc), if so, synchronize all of them
    // Since only view blocks with qc are checked, the latest view_block will not be synchronized (as it should be), but its qc will not be synchronized with the chain
    // This leads to inconsistent leader timeouts (because the leader iterates with qc)
    // Fortunately, this qc will be synchronized through highqc, so a commit operation is required when accepting highqc to ensure leader consistency

    // Find the last intersecting view_block
    View max_view = 0;
    std::shared_ptr<ViewBlock> cross_vb = nullptr;
    for (const auto& view_block: all) {
        auto cross_it = src_view_block_hash_set.find(view_block->qc().view_block_hash());
        if (cross_it != src_view_block_hash_set.end()) {
            if (!cross_vb || cross_vb->qc().view() < view_block->qc().view()) {
                cross_vb = view_block;
            }            
        }
    }

    std::vector<std::shared_ptr<ViewBlock>> vb_to_sync; // Blocks to be synchronized
    // When there is an intersection, only synchronize after the intersection
    if (cross_vb) {
        vb_to_sync.clear();
        // cross_vb is also synchronized back to ensure a legal chain
        vb_to_sync.push_back(cross_vb);
    } else {
        vb_to_sync = all;
    }

    for (const auto& view_block : vb_to_sync) {
        auto view_block_item = view_block_res->add_view_block_items();
        max_view = view_block->qc().view() > max_view ? view_block->qc().view() : max_view;
    }

    if (view_block_res->view_block_items_size() > 0) {
        shouldSyncChain = true;
    }

    // If there is only one block of cross_vb, it will not be synchronized (because cross_vb already exists)
    if (view_block_res->view_block_items_size() == 1 &&
        cross_vb &&
        view_block_res->view_block_items(0).qc().view_block_hash() == cross_vb->qc().view_block_hash()) {
        shouldSyncChain = false;
    }

    // If the maximum view of the local view_block_chain is < src node, it will not be synchronized
    if (max_view < src_max_view) {
        shouldSyncChain = false;
    }

    // Do not send messages
    if (!shouldSyncChain && !shouldSyncQC && !shouldSyncTC) {
        return Status::kSuccess;
    }

    if (!shouldSyncChain) {
        view_block_res->clear_view_block_items();
    }

    if (shouldSyncTC) {
        *view_block_res->mutable_high_tc() = *pacemaker(pool_idx)->HighTC();
    }

    if (shouldSyncQC) {
        *view_block_res->mutable_high_view_block() = *view_block_chain(pool_idx)->HighViewBlock();
    }

    // TODO If LatestCommittedblock is limited to Propose messages, it will cause the Leader to change, which will cause the replica to fail VerifyLeader and thus be unable to continue to participate in the consensus
    // In theory, only synchronizing the Chain to change the Leader is needed, but since ExpectedLeader is used, this problem can actually be solved, so I will add it here first
    // In addition, due to the single thread, when Propose sets LatestCommittedBlock, it will definitely continue to change the Chain, so there should be no situation where only LatestCommitBlock is synchronized without synchronizing the new Chain. Therefore, it is more stable in actual testing.
    if (shouldSyncLatestCommittedBlock) {
        *view_block_res->mutable_latest_committed_block() = *latest_committed_block;
    }
    
    return ReplyMsg(network_id, res_view_block_msg, msg_ptr);
}

Status HotstuffSyncer::processRequestSingle(const transport::MessagePtr& msg_ptr) {
    SHARDORA_DEBUG("handle message hash: %lu", msg_ptr->header.hash64());
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_single_req());

    uint32_t network_id = view_block_msg.single_req().network_id();
    uint32_t pool_idx = view_block_msg.single_req().pool_idx();

    if (common::GlobalInfo::Instance()->network_id() != network_id) {
        return Status::kError;
    }    

    auto chain = view_block_chain(pool_idx);
    if (!chain) {
        return Status::kError;
    }

    auto query_hash = view_block_msg.single_req().query_hash();
    // If query_hash does not exist, do not synchronize
    auto view_block_info = chain->Get(query_hash);
    if (!view_block_info) {
        return Status::kNotFound;
    }

    auto view_block = view_block_info->view_block;
    if (!view_block || !view_block->has_qc()) {
        return Status::kNotFound;
    }

    std::vector<std::shared_ptr<ViewBlock>> all;
    chain->GetAll(all);
    if (all.size() <= 0 || all.size() > kMaxSyncBlockNum) {
        return Status::kError;
    }

    // Check if there is a ViewBlock in the local ViewBlockChain that the src node does not have, if so, synchronize all of them
    transport::protobuf::Header msg;
    view_block::protobuf::ViewBlockSyncMessage&  res_view_block_msg = *msg.mutable_view_block_proto();
    auto& view_block_res = *res_view_block_msg.mutable_view_block_res();
    for (auto& view_block : all) {
        // Only synchronize view_blocks that already have qc
        auto view_block_item = view_block_res.add_view_block_items();
        *view_block_item = *view_block;
    }

    view_block_res.set_network_id(network_id);
    view_block_res.set_pool_idx(pool_idx);
    view_block_res.set_query_hash(query_hash); // Used to help src nodes filter redundancy
    res_view_block_msg.set_create_time_us(common::TimeUtils::TimestampUs());

    SHARDORA_DEBUG("pool: %d Send response single, block_size: %lu, network: %lu",
        pool_idx, view_block_res.view_block_items().size(), network_id);

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
        } else if (msg_ptr->header.view_block_proto().has_single_req()) {
            processRequestSingle(msg_ptr);
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
    SHARDORA_DEBUG("pool: %d, network: %lu, ip: %s, port: %d, with_query_hash: %d, hash64: %lu",
        view_block_msg.view_block_res().pool_idx(), network_id,
        ip.c_str(),
        port,
        msg.view_block_proto().view_block_res().has_query_hash(),
        msg.hash64());
    transport::TcpTransport::Instance()->Send(msg_ptr->conn.get(), msg);
    return Status::kSuccess; 
}

// Process response type messages
Status HotstuffSyncer::processResponse(const transport::MessagePtr& msg_ptr) {
    SHARDORA_DEBUG("handle hotstuff syncer message hash64: %lu", msg_ptr->header.hash64());
    auto& view_block_msg = msg_ptr->header.view_block_proto();
    assert(view_block_msg.has_view_block_res());
    uint32_t network_id = view_block_msg.view_block_res().network_id();
    uint32_t pool_idx = view_block_msg.view_block_res().pool_idx();
    
    if (common::GlobalInfo::Instance()->network_id() != network_id) {
        return Status::kError;
    }

    if (view_block_msg.view_block_res().has_query_hash()) {
        // Process single query
        // This view block already exists, return directly
        if (view_block_chain(pool_idx)->Has(view_block_msg.view_block_res().query_hash())) {
            return Status::kSuccess;
        }
    }
    
    processResponseQcTc(pool_idx, view_block_msg.view_block_res());
    processResponseLatestCommittedBlock(pool_idx, view_block_msg.view_block_res());
    Status s = processResponseChain(pool_idx, view_block_msg.view_block_res());
    if (s != Status::kSuccess) {
        return s;
    }
    // After synchronization, try to consume the previously failed Block
    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseQcTc(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    // Update highqc and hightc
    if (!view_block_res.has_high_view_block() && !view_block_res.has_high_tc()) {
        return Status::kSuccess;
    }

    auto pm = pacemaker(pool_idx);
    if (!pm) {
        return Status::kError;
    }

    std::shared_ptr<ViewBlock> high_view_block = nullptr;
    if (view_block_res.has_high_view_block()) {
        auto high_view_block = std::make_shared<ViewBlock>(view_block_res.high_view_block());
        if (high_view_block->qc().network_id() != common::GlobalInfo::Instance()->network_id() ||
                (view_block_res.has_high_tc() &&
                view_block_res.high_tc().network_id() != common::GlobalInfo::Instance()->network_id())) {
            SHARDORA_DEBUG("error network id hight qc: %u, hight tc: %u, local: %u",
                high_view_block->qc().network_id(), view_block_res.high_tc().network_id(), 
                common::GlobalInfo::Instance()->network_id());
            assert(false);
            return Status::kError;
        }

        // Set the qc of view_block
        // view_block_chain(pool_idx)->SetQcOf(highqc->view_block_hash(), highqc);
        SHARDORA_DEBUG("response received qctc pool_idx: %d, tc: %d, qc: %d",
            pool_idx, view_block_res.high_tc().view(), high_view_block->qc().view());
    }
    
    // TODO verify qc and tc
    // auto tc_ptr = std::make_shared<view_block::protobuf::QcItem>(view_block_res.high_tc());
    // pm->NewTc(tc_ptr);
    // Try to commit
    if (high_view_block) {
        SHARDORA_DEBUG("success new set qc view: %lu, %u_%u_%lu",
            high_view_block->qc().view(),
            high_view_block->qc().network_id(),
            high_view_block->qc().pool_index(),
            high_view_block->qc().view());
        pm->NewQcView(high_view_block->qc().view());
        transport::MessagePtr msg_ptr;
        hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(msg_ptr, high_view_block->qc());
    }

    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseLatestCommittedBlock(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    assert(false);
    // if (!view_block_res.has_latest_committed_block()) {
    //     return Status::kError;
    // }
    
    // auto& pb_latest_committed_block = view_block_res.latest_committed_block();
    // // May have been updated, no need to synchronize
    // auto cur_latest_committed_block = view_block_chain(pool_idx)->LatestCommittedBlock();
    // if (cur_latest_committed_block &&
    //         cur_latest_committed_block->qc().view() >= pb_latest_committed_block.qc().view()) {
    //     return Status::kSuccess;
    // }

    // SHARDORA_DEBUG("pool: %d sync latest committed block: %lu", pool_idx, pb_latest_committed_block.qc().view());
    // // Execute latest committed block
    // auto hf = hotstuff_mgr_->hotstuff(pb_latest_committed_block.qc().pool_index());
    // auto latest_vblock = std::make_shared<view_block::protobuf::ViewBlockItem>(pb_latest_committed_block);
    // hf->HandleSyncedViewBlock(latest_vblock);
    return Status::kSuccess;
}


Status HotstuffSyncer::processResponseChain(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    assert(false);
    return Status::kSuccess;
}

Status HotstuffSyncer::MergeChain(
        const uint32_t& pool_idx,
        std::shared_ptr<ViewBlockChain>& ori_chain,
        const std::shared_ptr<ViewBlockChain>& sync_chain,
        const ViewBlock& high_commit_qc) {
    // Find intersection
    std::vector<std::shared_ptr<ViewBlock>> view_blocks;
    ori_chain->GetOrderedAll(view_blocks);
    std::shared_ptr<ViewBlock> cross_block = nullptr;
    for (auto it = view_blocks.rbegin(); it != view_blocks.rend(); it++) {
        auto cross_block_info = sync_chain->Get((*it)->qc().view_block_hash());
        if (cross_block_info) {
            cross_block = cross_block_info->view_block;
            break;
        }
    }

    // If there is an intersection, you can add all sync_chain to ori_chain in turn
    // Duplicate blocks do not need to be added repeatedly, but they may carry commit qc, so you should try TryCommit
    // If the two chains have no intersection and cannot be connected, replace them with a chain with a larger max_view
    if (!cross_block) {
        // If the two chains have no intersection and cannot be connected, replace them with a chain with a larger max_view
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
        // If there is an intersection, the blocks before the intersection are not considered
        if (cross_block && sync_block->qc().view() < cross_block->qc().view()) {
            continue;
        }
        Status s = onRecViewBlock(pool_idx, ori_chain, *sync_block);
        if (s != Status::kSuccess) {
            SHARDORA_ERROR("pool: %d, merge chain block: %lu failed, s: %d",
                pool_idx, sync_block->qc().view(), (int32_t)s);
            continue;
        }
    }

    // Submit to high_commit_qc separately
    // Ensure that lagging nodes, although they do not have the latest proposal, have the latest qc, and the leader is consistent
    transport::MessagePtr msg_ptr;
    hotstuff_mgr_->hotstuff(pool_idx)->TryCommit(msg_ptr, high_commit_qc.qc());
    SHARDORA_DEBUG("0 success new set qc view: %lu, %u_%u_%lu",
            high_commit_qc.qc().view(),
            high_commit_qc.qc().network_id(),
            high_commit_qc.qc().pool_index(),
            high_commit_qc.qc().view());
    pacemaker(pool_idx)->NewQcView(high_commit_qc.qc().view());
    return Status::kSuccess;
}

// As long as the QC verification is successful, there is no need to verify others, and the ViewBlock is directly accepted
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
    SHARDORA_DEBUG("success new set qc view: %lu, %u_%u_%lu",
        view_block_ptr->qc().view(),
        view_block_ptr->qc().network_id(),
        view_block_ptr->qc().pool_index(),
        view_block_ptr->qc().view());
    hotstuff->pacemaker()->NewQcView(view_block_ptr->qc().view());
    transport::MessagePtr msg_ptr;
    hotstuff->TryCommit(msg_ptr, view_block.qc());
    // If this block already exists, return directly
    if (view_block_chain(pool_idx)->Has(view_block.qc().view_block_hash())) {
        return Status::kSuccess;
    }

    // Verify transaction
    auto accep = hotstuff->acceptor();
    if (!accep) {
     
        return Status::kError;
    }
    s = accep->AcceptSync(*view_block_ptr);
    if (s != Status::kSuccess) {
        SHARDORA_ERROR("pool: %d sync accept failed", pool_idx);
        return s;
    }    

    // 4. Save view_block
    // TODO: check valid
    s = hotstuff->view_block_chain()->Store(view_block_ptr, true, nullptr, nullptr, false);
    if (s != Status::kSuccess) {
        SHARDORA_ERROR("pool: %d store view block failed, hash: %s, view: %lu, cur chain: %s", pool_idx,
            common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(), 
            view_block.qc().view(), 
            view_block_chain(pool_idx)->String().c_str());
        return s;
    }

    return Status::kSuccess;
}

} // namespace consensus

} // namespace shardora
