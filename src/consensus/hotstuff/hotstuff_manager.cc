#include "hotstuff_manager.h"
#include "leader_rotation.h"

#include <cassert>
#include <chrono>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <libbls/tools/utils.h>
#include <protos/pools.pb.h>

#include "bls/bls_utils.h"
#include "bls/bls_manager.h"
#include "bls/bls_sign.h"
#include "common/hash.h"
#include "common/global_info.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "common/encode.h"
#include "db/db.h"
#include "dht/base_dht.h"
#include "elect/elect_manager.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "pools/tx_pool_manager.h"
#include "transport/processor.h"
#include "types.h"
namespace shardora {

namespace consensus {

namespace {
    uint32_t kNodeMax = 1024;
    enum MsgType {
        PROPOSE,
        VOTE,
    };
}

HotstuffManager::HotstuffManager() {}

HotstuffManager::~HotstuffManager() {
}
// todo
void HotstuffManager::RegisterCreateTxCallbacks() {
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kNormalFrom,
    //     std::bind(&HotstuffManager::CreateFromTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kNormalTo,
    //     std::bind(&HotstuffManager::CreateToTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kConsensusLocalTos,
    //     std::bind(&HotstuffManager::CreateToTxLocal, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kContractCreateByRootTo,
    //     std::bind(&HotstuffManager::CreateContractByRootToTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kRootCreateAddress,
    //     std::bind(&HotstuffManager::CreateRootToTxItem, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kContractCreate,
    //     std::bind(&HotstuffManager::CreateContractUserCreateCallTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kContractCreateByRootFrom,
    //     std::bind(&HotstuffManager::CreateContractByRootFromTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kContractGasPrepayment,
    //     std::bind(&HotstuffManager::CreateContractUserCallTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kContractExcute,
    //     std::bind(&HotstuffManager::CreateContractCallTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kJoinElect,
    //     std::bind(&HotstuffManager::CreateJoinElectTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kCreateLibrary,
    //     std::bind(&HotstuffManager::CreateLibraryTx, this, std::placeholders::_1));
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kRootCross,
    //     std::bind(&HotstuffManager::CreateRootCrossTx, this, std::placeholders::_1));
    // block_mgr_->SetCreateToTxFunction(
    //     std::bind(&HotstuffManager::CreateToTx, this, std::placeholders::_1));
    // block_mgr_->SetCreateStatisticTxFunction(
    //     std::bind(&HotstuffManager::CreateStatisticTx, this, std::placeholders::_1));
    // block_mgr_->SetCreateElectTxFunction(
    //     std::bind(&HotstuffManager::CreateElectTx, this, std::placeholders::_1));
    // block_mgr_->SetCreateCrossTxFunction(
    //     std::bind(&HotstuffManager::CreateCrossTx, this, std::placeholders::_1));
    // tm_block_mgr_->SetCreateTmTxFunction(
    //     std::bind(&HotstuffManager::CreateTimeblockTx, this, std::placeholders::_1));
}

int HotstuffManager::Init(
        block::BlockAggValidCallback block_agg_valid_func,
        std::shared_ptr<contract::ContractManager>& contract_mgr,
        std::shared_ptr<consensus::ContractGasPrepayment>& gas_prepayment,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<db::Db>& db,
        BlockCallback block_cb, // 无用处，可不传
        uint8_t thread_count, // 无用处，可不传
        BlockCacheCallback new_block_cache_callback) {
    block_agg_valid_func_ = block_agg_valid_func;
    contract_mgr_ = contract_mgr;
    gas_prepayment_ = gas_prepayment;
    vss_mgr_ = vss_mgr;
    account_mgr_ = account_mgr;
    block_mgr_ = block_mgr;
    elect_mgr_ = elect_mgr;
    pools_mgr_ = pool_mgr;
    security_ptr_ = security_ptr;
    tm_block_mgr_ = tm_block_mgr;
    bls_mgr_ = bls_mgr;
    kv_sync_ = kv_sync;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    new_block_cache_callback_ = new_block_cache_callback;
    
    // txs_pools_ = std::make_shared<WaitingTxsPools>(pools_mgr_, block_mgr, tm_block_mgr);
    // bft_queue_ = new std::queue<ZbftPtr>[common::kMaxThreadCount];
    // elect_items_[0] = std::make_shared<ElectItem>();
    // elect_items_[1] = std::make_shared<ElectItem>();

    elect_info_ = std::make_shared<ElectInfo>(security_ptr);
    crypto_ = std::make_shared<Crypto>(elect_info_, bls_mgr);
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        PoolManager pool_manager;
        pool_manager.view_block_chain = std::make_shared<ViewBlockChain>();
        auto leader_rotation = std::make_shared<LeaderRotation>(pool_manager.view_block_chain, elect_info_);
        auto pace_maker = std::make_shared<Pacemaker>(pool_idx, crypto_, leader_rotation, std::make_shared<ViewDuration>());
        pool_manager.block_acceptor = std::make_shared<BlockAcceptor>(pool_idx, security_ptr, account_mgr, elect_info_, vss_mgr,
            contract_mgr, db, gas_prepayment, pool_mgr, block_mgr, tm_block_mgr, new_block_cache_callback);
        pool_managers_[pool_idx] = pool_manager;
    }

    RegisterCreateTxCallbacks();

    network::Route::Instance()->RegisterMessage(
        common::kConsensusMessage,
        std::bind(&HotstuffManager::HandleMessage, this, std::placeholders::_1));

    return kConsensusSuccess;
}


int HotstuffManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void HotstuffManager::OnNewElectBlock(uint64_t block_tm_ms, uint32_t sharding_id, uint64_t elect_height,
    common::MembersPtr& members, const libff::alt_bn128_G2& common_pk, const libff::alt_bn128_Fr& sec_key) {
        elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
    }


void HotstuffManager::DoCommitBlock(const view_block::protobuf::ViewBlockItem& pb_view_block, const uint32_t& pool_index) {
    std::shared_ptr<ViewBlock> v_block;
    Proto2ViewBlock(pb_view_block, v_block);
    auto qc = v_block->qc;
    std::shared_ptr<ViewBlock> pre_vb;
    auto view_block_chain = pool_managers_[pool_index].view_block_chain;
    view_block_chain->Get(pb_view_block.parent_hash(), pre_vb);
    auto f_view = qc->view;
    auto s_viwe = pre_vb->qc->view;
    if (f_view == s_viwe + 1) {
        auto t_view = s_viwe -1;
        auto p_pre_hash = pre_vb->parent_hash;
        std::shared_ptr<ViewBlock> p_pre_vb;
        if (view_block_chain->Has(p_pre_hash)) {
            view_block_chain->Get(p_pre_hash, p_pre_vb);
            if (p_pre_vb->qc->view == t_view) {
                // todo 执行commit块之间的交易信息
                std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
                view_block_chain->PruneTo(p_pre_vb->hash, forked_blockes, true);
                // todo forked_blockes交易回退
                view_block_chain->SetLatestCommittedBlock(p_pre_vb);
                view_block_chain->SetLatestLockedBlock(pre_vb);
            }
        }
    } 
}

Status HotstuffManager::VerifyViewBlockItem(const std::shared_ptr<ViewBlock>& v_block, 
    const std::shared_ptr<ViewBlockChain>& view_block_chain, const uint32_t& elect_height) {
    Status ret = Status::kSuccess;
    auto block_view = v_block->view;

    if (!v_block->Valid()) {
        ZJC_ERROR("view block hash is error.");
        return Status::kError;
    }
    auto qc = v_block->qc;
    if (crypto_->Verify(elect_height, qc->view_block_hash, qc->bls_agg_sign) != Status::kSuccess) {
        ZJC_ERROR("Verify qc is error.");
        return Status::kError; 
    }
    if (block_view != qc->view + 1) {
        ZJC_ERROR("qc view is error.");
        return Status::kError; 
    }

    // hotstuff condition
    std::shared_ptr<ViewBlock> qc_view_block;
    if (view_block_chain->Get(qc->view_block_hash, qc_view_block) != Status::kSuccess 
        && !view_block_chain->Extends(v_block, qc_view_block)) {
        ZJC_ERROR("qc view block message is error.");
        return Status::kError;
    }
    if (!view_block_chain->Extends(v_block, view_block_chain->LatestLockedBlock()) && 
        v_block->qc->view <= view_block_chain->LatestLockedBlock()->view) {
        ZJC_ERROR("block view message is error.");
        return Status::kError;
    }
    

    return ret;
}

void HotstuffManager::DoVoteMsg(const hotstuff::protobuf::ProposeMsg& pro_msg, const uint32_t& pool_index) {
    view_block::protobuf::ViewBlockItem pb_view_block = pro_msg.view_item();
    uint32_t leader_idx = pb_view_block.leader_idx();
    auto leader_rotation = std::make_shared<LeaderRotation>(pool_managers_[pool_index].view_block_chain, elect_info_);
    if (leader_idx != leader_rotation->GetLeader()->index) {
        ZJC_ERROR("leader_idx message is error.");
        return;
    }

    auto view_block_chain = pool_managers_[pool_index].view_block_chain;
    if (view_block_chain->GetMaxHeight() >= pb_view_block.view()) {
        ZJC_ERROR("block view is error.");
        return;
    }

    std::shared_ptr<ViewBlock> v_block;
    Status s = Proto2ViewBlock(pb_view_block, v_block);
    if (s != Status::kSuccess) {
        return;
    }
    
    if (VerifyViewBlockItem(v_block, view_block_chain, pro_msg.elect_height()) != Status::kSuccess) {
        ZJC_ERROR("VerifyViewBlockItem is error.");
        return;
    }
    
    
    if (view_block_chain->Store(v_block) != Status::kSuccess) {
        ZJC_ERROR("add view block error.");
        return;
    }    
    block::protobuf::Block block;
    if (!block.ParseFromString(pb_view_block.block_str())) {
        ZJC_ERROR("block_str message is error.");
        // return Status::kError;
    }
    // todo ret = block 交易信息验证 + 执行（kv内存）
    // 拆分
    // 1、验证是否存在3个连续qc，设置commit，lock qc状态；2、提交commit块之间的交易信息；3、减枝保留最新commit块，回退分支的交易信息
    DoCommitBlock(pb_view_block, pool_index);

    // 切换视图
    Proto2ViewBlock(pb_view_block, v_block);
    auto qc = v_block->qc;
    auto sync_info = std::make_shared<SyncInfo>();
    sync_info->qc = qc;
    pool_managers_[pool_index].pace_maker->AdvanceView(sync_info);

    hotstuff::protobuf::VoteMsg vote_msg;
    uint32_t replica_idx = elect_info_->GetElectItem()->LocalMember()->index;
    vote_msg.set_replica_idx(replica_idx);
    // todo 执行tx，block发生变化，更新view_block hash

    view_block::protobuf::ViewBlockItem view_block = pro_msg.view_item();
    HashStr view_block_hash = view_block.hash();
    vote_msg.set_view_block_hash(view_block_hash);
    uint32_t elect_height = pro_msg.elect_height();
    vote_msg.set_elect_height(elect_height);
    std::string sign_x, sign_y;
    if (crypto_->Sign(elect_height, view_block_hash,&sign_x, &sign_y) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return;
    }
    vote_msg.set_sign_x(sign_x);
    vote_msg.set_sign_x(sign_y);
    // todo 附加交易池的消息
    
    hotstuff::protobuf::HotstuffMessage  hotstuff_msg;
    hotstuff_msg.set_type(VOTE); 
    hotstuff_msg.set_allocated_vote_msg(&vote_msg);
    hotstuff_msg.set_net_id(common::GlobalInfo::Instance()->network_id());
    hotstuff_msg.set_pool_index(pool_index);

    if (SendTranMsg(hotstuff_msg) != Status::kSuccess) {
        ZJC_ERROR("Send Propose message is error.");
    }
    return;
}


Status HotstuffManager::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index,  
    std::shared_ptr<ViewBlock>& view_block) {
    uint32_t replica_idx = vote_msg.replica_idx();
    if (replica_idx > kNodeMax) {
        ZJC_ERROR("replica_idx message is error.");
        return Status::kError;
    }
    // 1、根据hash查找view_block；2、view_block.view <= heighQC.view
    if (!pool_managers_[pool_index].view_block_chain->Has(vote_msg.view_block_hash())) {
        ZJC_ERROR("view_block_hash message is not exited.");
        return Status::kError;
    }
    pool_managers_[pool_index].view_block_chain->Get(vote_msg.view_block_hash(), view_block);
    if (view_block->view > pool_managers_[pool_index].pace_maker->HighQC()->view) {
        ZJC_ERROR("view message is not exited.");
        return Status::kError;
    }
    return Status::kSuccess;
}

Status HotstuffManager::SendTranMsg(hotstuff::protobuf::HotstuffMessage& hotstuff_msg) {
    transport::protobuf::Header header;
    header.set_allocated_hotstuff(&hotstuff_msg);
    Status ret = Status::kSuccess;
    // todo ret = send msg 
    return ret;
}

void HotstuffManager::DoProposeMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index) {
    std::shared_ptr<ViewBlock> v_block;
    if (VerifyVoteMsg(vote_msg, pool_index, v_block) != Status::kSuccess) {
        ZJC_ERROR("vote message is error.");
        return;
    }

    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();
    auto view_block_hash = vote_msg.view_block_hash();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    Status ret = crypto_->ReconstructAndVerify(elect_height, v_block->view, view_block_hash, replica_idx, 
        vote_msg.sign_x(), vote_msg.sign_y(), reconstructed_sign);
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_INFO("kBlsVerifyWaiting");
            return;
        }
        ZJC_ERROR("ReconstructAndVerify error");
        return;
    }
    auto high_view = pool_managers_[pool_index].pace_maker->HighQC()->view;
    auto qc = std::make_shared<QC>(reconstructed_sign, high_view + 1, view_block_hash);

    view_block::protobuf::ViewBlockItem view_block;
    view_block.set_parent_hash(view_block_hash);
    auto leader_idx = elect_info_->GetElectItem()->LocalMember()->index;
    view_block.set_leader_idx(leader_idx);
    std::shared_ptr<block::protobuf::Block> pb_block;
    std::string block_str;
    // todo 获取vote msg中的tx信息
    // todo construct block 打包交易信息 
    pb_block->SerializeToString(&block_str);
    
    view_block.set_block_str(block_str);
    view_block.set_qc_str(qc->Serialize());
    view_block.set_view(qc->view + 1);

    HashStr hash = ViewBlock(view_block_hash,qc,pb_block,qc->view + 1,leader_idx).DoHash();
    view_block.set_hash(hash);

    hotstuff::protobuf::ProposeMsg pro_msg;
    pro_msg.set_allocated_view_item(&view_block);
    pro_msg.set_elect_height(elect_info_->GetElectItem()->ElectHeight());

    hotstuff::protobuf::HotstuffMessage  hotstuff_msg;
    hotstuff_msg.set_type(PROPOSE); 
    hotstuff_msg.set_allocated_pro_msg(&pro_msg);
    hotstuff_msg.set_net_id(common::GlobalInfo::Instance()->network_id());
    hotstuff_msg.set_pool_index(pool_index);

    if (SendTranMsg(hotstuff_msg) != Status::kSuccess) {
        ZJC_ERROR("Send Propose message is error.");
    }
    return;
}

void HotstuffManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    assert(header.type() == common::kConsensusMessage);

    if (!header.has_hotstuff()) {
        ZJC_ERROR("transport message is error.");
        return;
    }
    auto& hotstuff_msg = header.hotstuff();
    if (hotstuff_msg.net_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_ERROR("net_id is error.");
        return;
    }
    if (hotstuff_msg.pool_index() >= common::kInvalidPoolIndex) {
        ZJC_ERROR("pool index invalid[%d]!", hotstuff_msg.pool_index());
        return ;
    }
    switch (hotstuff_msg.type())
    {
    case PROPOSE:
        DoVoteMsg(hotstuff_msg.pro_msg(), hotstuff_msg.pool_index());
        break;
    case VOTE:
        DoProposeMsg(hotstuff_msg.vote_msg(), hotstuff_msg.pool_index());
        break;
    default:
        ZJC_WARN("consensus message type is error.");
        break;
    }
}

}  // namespace consensus

}  // namespace shardora
