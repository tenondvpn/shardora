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
}

// 批量异步处理，提高 tps
void HotstuffSyncer::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    // TODO: test
    if (!running_) {
        return;
    }
    // TODO 仅共识池节点参与 view_block_chain 的同步
    HandleSyncedBlocks(msg_ptr);
}

void HotstuffSyncer::SyncPool(const uint32_t& pool_idx, const int32_t& node_num) {
}

void HotstuffSyncer::SyncAllPools() {
}

// No use, about to deprecate
void HotstuffSyncer::SyncViewBlock(const uint32_t& pool_idx, const HashStr& hash) {
}

void HotstuffSyncer::HandleSyncedBlocks(const transport::MessagePtr& msg_ptr) {
    // auto thread_idx = (uint32_t)common::GlobalInfo::Instance()->get_thread_index();
    // if (thread_idx != 1) {
    //     return;
    // }
    // auto now_thread_id_tmp = std::this_thread::get_id();
    // uint32_t now_thread_id = *(uint32_t*)&now_thread_id_tmp;

    // common::ThreadSafeQueue<std::shared_ptr<ViewBlock>> vblock_queues[common::kMaxThreadCount];
    // ZJC_DEBUG("now_thread_id: %u, thread_idx: %u", now_thread_id, thread_idx);
    auto& block_queue = kv_sync_->vblock_queue();
    // common::ThreadSafeQueue<std::shared_ptr<ViewBlock>> block_queue;
    std::shared_ptr<view_block::protobuf::ViewBlockItem> pb_vblock = nullptr;
    while (block_queue.pop(&pb_vblock)) {
        if (pb_vblock) {
            hotstuff_mgr_->hotstuff(pb_vblock->qc().pool_index())->HandleSyncedViewBlock(
                    pb_vblock);
        }
    }    
    // ZJC_DEBUG("over now_thread_id: %u, thread_idx: %u", now_thread_id, thread_idx);
}

Status HotstuffSyncer::Broadcast(const view_block::protobuf::ViewBlockSyncMessage& view_block_msg) {
    return Status::kSuccess;
}

Status HotstuffSyncer::SendRequest(uint32_t network_id, view_block::protobuf::ViewBlockSyncMessage& view_block_msg, int32_t node_num) {
    return Status::kSuccess;
}

// 处理 request 类型消息
Status HotstuffSyncer::processRequest(const transport::MessagePtr& msg_ptr) {
    return Status::kError;
}

Status HotstuffSyncer::processRequestSingle(const transport::MessagePtr& msg_ptr) {
    return Status::kError;
}

void HotstuffSyncer::ConsumeMessages() {
   
}

Status HotstuffSyncer::ReplyMsg(
        uint32_t network_id,
        const view_block::protobuf::ViewBlockSyncMessage& view_block_msg,
        const transport::MessagePtr& msg_ptr) {
    
}

// 处理 response 类型消息
Status HotstuffSyncer::processResponse(const transport::MessagePtr& msg_ptr) {
    
    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseQcTc(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
    return Status::kSuccess;
}

Status HotstuffSyncer::processResponseLatestCommittedBlock(
        const uint32_t& pool_idx,
        const view_block::protobuf::ViewBlockSyncResponse& view_block_res) {
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
    return Status::kSuccess;
}

// 只要 QC 验证成功，无需验证其他，直接接受该 ViewBlock
Status HotstuffSyncer::onRecViewBlock(
        const uint32_t& pool_idx,
        const std::shared_ptr<ViewBlockChain>& ori_chain,
        const ViewBlock& view_block) {
    return Status::kSuccess;
}

} // namespace consensus

} // namespace shardora

