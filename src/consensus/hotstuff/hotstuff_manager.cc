#include "hotstuff_manager.h"
#include "leader_rotation.h"

#include <cassert>
#include <chrono>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <libbls/tools/utils.h>
#include <protos/pools.pb.h>
#include <sys/socket.h>

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
typedef  hotstuff::protobuf::ProposeMsg  pb_ProposeMsg;
typedef  hotstuff::protobuf::HotstuffMessage  pb_HotstuffMessage;
typedef  hotstuff::protobuf::VoteMsg  pb_VoteMsg;
std::shared_ptr<pb_HotstuffMessage> ConstructHotstuffMsg(const MsgType msg_type, 
    const std::shared_ptr<pb_ProposeMsg>& pb_pro_msg, 
    const std::shared_ptr<pb_VoteMsg>& pb_vote_msg, 
    const uint32_t pool_index) {
    auto pb_hotstuff_msg = std::make_shared<pb_HotstuffMessage>();
    pb_hotstuff_msg->set_type(msg_type);
    switch (msg_type)
    {
    case PROPOSE:
        pb_hotstuff_msg->set_allocated_pro_msg(pb_pro_msg.get());
        break;
    case VOTE:
        pb_hotstuff_msg->set_allocated_vote_msg(pb_vote_msg.get());
        break;
    default:
        ZJC_ERROR("MsgType is error");
        break;
    }
    pb_hotstuff_msg->set_net_id(common::GlobalInfo::Instance()->network_id());
    pb_hotstuff_msg->set_pool_index(pool_index);
    return pb_hotstuff_msg;
}
}

HotstuffManager::HotstuffManager() {}

HotstuffManager::~HotstuffManager() {}

void HotstuffManager::RegisterCreateTxCallbacks() {
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kNormalFrom,
        std::bind(&HotstuffManager::CreateFromTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kNormalTo,
        std::bind(&HotstuffManager::CreateToTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kConsensusLocalTos,
        std::bind(&HotstuffManager::CreateToTxLocal, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreateByRootTo,
        std::bind(&HotstuffManager::CreateContractByRootToTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kRootCreateAddress,
        std::bind(&HotstuffManager::CreateRootToTxItem, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreate,
        std::bind(&HotstuffManager::CreateContractUserCreateCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractCreateByRootFrom,
        std::bind(&HotstuffManager::CreateContractByRootFromTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractGasPrepayment,
        std::bind(&HotstuffManager::CreateContractUserCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractExcute,
        std::bind(&HotstuffManager::CreateContractCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kJoinElect,
        std::bind(&HotstuffManager::CreateJoinElectTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kCreateLibrary,
        std::bind(&HotstuffManager::CreateLibraryTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kRootCross,
        std::bind(&HotstuffManager::CreateRootCrossTx, this, std::placeholders::_1));
    block_mgr_->SetCreateToTxFunction(
        std::bind(&HotstuffManager::CreateToTx, this, std::placeholders::_1));
    block_mgr_->SetCreateStatisticTxFunction(
        std::bind(&HotstuffManager::CreateStatisticTx, this, std::placeholders::_1));
    block_mgr_->SetCreateElectTxFunction(
        std::bind(&HotstuffManager::CreateElectTx, this, std::placeholders::_1));
    block_mgr_->SetCreateCrossTxFunction(
        std::bind(&HotstuffManager::CreateCrossTx, this, std::placeholders::_1));
    tm_block_mgr_->SetCreateTmTxFunction(
        std::bind(&HotstuffManager::CreateTimeblockTx, this, std::placeholders::_1));
}

int HotstuffManager::Init(
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
        std::shared_ptr<db::Db>& db,
        BlockCacheCallback new_block_cache_callback) {
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
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);

    elect_info_ = std::make_shared<ElectInfo>(security_ptr, elect_mgr_);
    
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        HotStuff hf;
        auto crypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
        auto chain = std::make_shared<ViewBlockChain>();
        auto leader_rotation = std::make_shared<LeaderRotation>(chain, elect_info_);
        auto pace_maker = std::make_shared<Pacemaker>(pool_idx, crypto, leader_rotation, std::make_shared<ViewDuration>());
        // set pacemaker timeout callback function
        pace_maker->SetNewProposalFn(std::bind(&HotstuffManager::Propose,
                this, std::placeholders::_1, std::placeholders::_2));

        // create hotstuff for each pool
        hf.pool_idx = pool_idx;
        hf.leader_rotation = leader_rotation;
        hf.view_block_chain = chain; 
        hf.block_acceptor = std::make_shared<BlockAcceptor>(pool_idx, security_ptr, account_mgr, elect_info_, vss_mgr,
            contract_mgr, db, gas_prepayment, pool_mgr, block_mgr, tm_block_mgr, new_block_cache_callback);
        hf.pace_maker = pace_maker;
        hf.crypto = crypto;
        // 初始化
        hf.Init(db_);
        pool_hotstuff_[pool_idx] = hf;
    }

    RegisterCreateTxCallbacks();

    network::Route::Instance()->RegisterMessage(common::kHotstuffMessage,
        std::bind(&HotstuffManager::HandleMessage, this, std::placeholders::_1));
    network::Route::Instance()->RegisterMessage(common::kHotstuffTimeoutMessage,
        std::bind(&HotstuffManager::HandleMessage, this, std::placeholders::_1));    

    return kConsensusSuccess;
}

// start chained hotstuff
Status HotstuffManager::Start() {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        auto hf = hotstuff(pool_idx);
        auto leader = hf->leader_rotation->GetLeader();
        auto elect_item = elect_info_->GetElectItem();
        if (!elect_item) {
            return Status::kElectItemNotFound;
        }
        auto local_member = elect_item->LocalMember();
        if (!local_member) {
            return Status::kError;
        }
        if (!leader) {
            ZJC_ERROR("Get Leader is error.");
        } else if (leader->index == local_member->index) {
            ZJC_INFO("ViewBlock start propose");
            Propose(pool_idx, new_sync_info()->WithQC(pacemaker(pool_idx)->HighQC()));
        }   
    }
    return Status::kSuccess;
}


int HotstuffManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void HotstuffManager::OnNewElectBlock(uint64_t block_tm_ms, uint32_t sharding_id, uint64_t elect_height,
    common::MembersPtr& members, const libff::alt_bn128_G2& common_pk, const libff::alt_bn128_Fr& sec_key) {        
        elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
    }


Status HotstuffManager::Commit(const std::shared_ptr<ViewBlock>& v_block, const uint32_t& pool_index) {
    auto c = chain(pool_index);
    auto accp = acceptor(pool_index);
    if (!c || !accp) {
        return Status::kError;
    }
    // 递归提交
    Status s = CommitInner(c, acceptor(pool_index), v_block);
    if (s != Status::kSuccess) {
        return s;
    }
    // 剪枝
    std::vector<std::shared_ptr<ViewBlock>> forked_blockes;
    s = c->PruneTo(v_block->hash, forked_blockes, true);
    if (s != Status::kSuccess) {
        return s;
    }

    // 归还分支交易
    for (const auto& forked_block : forked_blockes) {
        s = accp->Return(forked_block->block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    return Status::kSuccess;
}

Status HotstuffManager::CommitInner(
        const std::shared_ptr<ViewBlockChain>& c,
        const std::shared_ptr<IBlockAcceptor> accp, 
        const std::shared_ptr<ViewBlock>& v_block) {
    if (!c) {
        return Status::kError;
    }

    auto latest_committed_block = c->LatestCommittedBlock();
    if (latest_committed_block && latest_committed_block->view >= v_block->view) {
        return Status::kSuccess;
    }

    if (!latest_committed_block && v_block->view == GenesisView) {
        return Status::kSuccess;
    }

    std::shared_ptr<ViewBlock> parent_block = nullptr;
    Status s = c->Get(v_block->parent_hash, parent_block);
    if (s == Status::kSuccess && parent_block != nullptr) {
        s = CommitInner(c, accp, parent_block);
        if (s != Status::kSuccess) {
            return s;
        }
    }

    if (!accp) {
        return Status::kError;
    }

    s = accp->Commit(v_block->block);
    if (s != Status::kSuccess) {
        return s;
    }
    
    c->SetLatestCommittedBlock(v_block);
    return Status::kSuccess;
}

std::shared_ptr<ViewBlock> HotstuffManager::CheckCommit(
        const std::shared_ptr<ViewBlock>& v_block,
        const uint32_t& pool_index) {
    auto c = chain(pool_index);
    if (!c) {
        return nullptr;
    }
    auto v_block2 = c->QCRef(v_block);
    if (!v_block2) {
        return nullptr;
    }

    if (!c->LatestLockedBlock() || v_block2->view > c->LatestLockedBlock()->view) {
        ZJC_DEBUG("locked block, view: %lu", v_block2->view);
        c->SetLatestLockedBlock(v_block2);
    }

    auto v_block3 = c->QCRef(v_block2);
    if (!v_block3) {
        return nullptr;
    }

    if (v_block->parent_hash == v_block2->hash && v_block2->parent_hash == v_block3->hash) {
        ZJC_DEBUG("decide block, view: %lu", v_block3->view);
        return v_block3;
    }
    
    return nullptr;
}

Status HotstuffManager::VerifyViewBlock(
        const uint32_t& pool_idx,
        const std::shared_ptr<ViewBlock>& v_block, 
        const std::shared_ptr<ViewBlockChain>& view_block_chain,
        const uint32_t& elect_height) {
    Status ret = Status::kSuccess;
    auto block_view = v_block->view;
    if (!v_block->Valid()) {
        ZJC_ERROR("view block hash is error.");
        return Status::kError;
    }
    // view 必须最新
    if (view_block_chain->GetMaxHeight() >= v_block->view) {
        ZJC_ERROR("block view is error.");
        return Status::kError;
    }
    
    auto qc = v_block->qc;
    if (!qc) {
        ZJC_ERROR("qc 必须存在.");
        return Status::kError;
    }
    
    // qc 指针和哈希指针一致
    if (qc->view_block_hash != v_block->parent_hash) {
        ZJC_ERROR("qc ref is different from hash ref");
        return Status::kError;        
    }

    // 验证 qc
    if (crypto(pool_idx)->VerifyQC(qc, elect_height) != Status::kSuccess) {
        ZJC_ERROR("Verify qc is error.");
        return Status::kError; 
    }

    // hotstuff condition
    std::shared_ptr<ViewBlock> qc_view_block;
    if (view_block_chain->Get(qc->view_block_hash, qc_view_block) != Status::kSuccess 
        && !view_block_chain->Extends(v_block, qc_view_block)) {
        ZJC_ERROR("qc view block message is error.");
        return Status::kError;
    }

    if (view_block_chain->LatestLockedBlock() &&
        !view_block_chain->Extends(v_block, view_block_chain->LatestLockedBlock()) && 
        v_block->qc->view <= view_block_chain->LatestLockedBlock()->view) {
        ZJC_ERROR("block view message is error.");
        return Status::kError;
    }   

    return ret;
}

Status HotstuffManager::VerifyLeader(const uint32_t& pool_index, const std::shared_ptr<ViewBlock>& view_block) {
    uint32_t leader_idx = view_block->leader_idx;
    auto leader_rotation = std::make_shared<LeaderRotation>(chain(pool_index), elect_info_);
    auto leader = leader_rotation->GetLeader(); // 判断是否为空
    if (!leader) {
        ZJC_ERROR("Get Leader is error.");
        return  Status::kError;
    }
    if (leader_idx != leader->index) {
        ZJC_ERROR("leader_idx message is error.");
        return Status::kError;
    }
    return Status::kSuccess;
}

void HotstuffManager::HandleProposeMsg(const hotstuff::protobuf::ProposeMsg& pro_msg, const uint32_t& pool_index) {
    // 1 校验pb view block格式
    view_block::protobuf::ViewBlockItem pb_view_block = pro_msg.view_item();
    std::shared_ptr<ViewBlock> v_block;
    Status s = Proto2ViewBlock(pb_view_block, v_block);
    if (s != Status::kSuccess) {
        ZJC_ERROR("pb_view_block to ViewBlock is error.");
        return;
    }

    // 已经投过票
    if (hotstuff(pool_index)->HasVoted(v_block->view)) {
        return;
    }
    
    // 2 Veriyfy Leader
    if (VerifyLeader(pool_index, v_block) != Status::kSuccess) {
        return;
    }

    // 3 Verify TC
    auto tc = std::make_shared<TC>();
    if (!pro_msg.tc_str().empty() && !tc->Unserialize(pro_msg.tc_str())) {
        ZJC_ERROR("tc Unserialize is error.");
        return;
    }
    if (crypto(pool_index)->VerifyTC(tc, pro_msg.elect_height()) != Status::kSuccess) {
        return;
    }
    
    // 4 Verify ViewBlock    
    if (VerifyViewBlock(pool_index, v_block, chain(pool_index), pro_msg.elect_height()) != Status::kSuccess) {
        ZJC_ERROR("Verify ViewBlock is error. hash: %s",
            common::Encode::HexEncode(v_block->hash).c_str());
        return;
    }

    // 切换视图
    pacemaker(pool_index)->AdvanceView(new_sync_info()->WithTC(tc)->WithQC(v_block->qc));
    
    // 5 Verify ViewBlock.block and tx_propose, 验证tx_propose，填充Block tx相关字段
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    auto block = v_block->block;
    block_info->block = block;
    block_info->tx_type = pro_msg.tx_propose().tx_type();
    for (int i = 0; i < pro_msg.tx_propose().txs_size(); i++)
    {
        auto tx = std::make_shared<pools::protobuf::TxMessage>(pro_msg.tx_propose().txs(i));
        block_info->txs.push_back(tx);
    }
    block_info->view = v_block->view;
    
    if (acceptor(pool_index)->Accept(block_info) != Status::kSuccess) {
        // 归还交易
        acceptor(pool_index)->Return(block_info->block);
        ZJC_ERROR("Accept tx is error");
        return;
    }
    // 更新哈希值
    v_block->UpdateHash();
    
    // 6 add view block
    if (chain(pool_index)->Store(v_block) != Status::kSuccess) {
        ZJC_ERROR("add view block error. hash: %s",
            common::Encode::HexEncode(v_block->hash).c_str());
        return;
    }

    // 1、验证是否存在3个连续qc，设置commit，lock qc状态；2、提交commit块之间的交易信息；3、减枝保留最新commit块，回退分支的交易信息
    auto v_block_to_commit = CheckCommit(v_block, pool_index);
    if (v_block_to_commit) {
        Status s = Commit(v_block_to_commit, pool_index);
        if (s != Status::kSuccess) {
            ZJC_ERROR("commit view_block failed, view: %lu hash: %s",
                v_block_to_commit->view,
                common::Encode::HexEncode(v_block_to_commit->hash).c_str());
            return;
        }
    }
    
    // Construct VoteMsg
    auto vote_msg = std::make_shared<hotstuff::protobuf::VoteMsg>();
    s = ConstructVoteMsg(vote_msg, pro_msg.elect_height(), v_block, pool_index);
    if (s != Status::kSuccess) {
        return;
    }
    // Construct HotstuffMessage and send
    auto pb_hotstuff_msg = ConstructHotstuffMsg(VOTE, nullptr, vote_msg, pool_index);
    if (SendTranMsg(pool_index, pb_hotstuff_msg) != Status::kSuccess) {
        ZJC_ERROR("Send Propose message is error.");
    }
    
    return;
}

Status HotstuffManager::ConstructVoteMsg(std::shared_ptr<hotstuff::protobuf::VoteMsg>& vote_msg, const uint32_t& elect_height, 
    const std::shared_ptr<ViewBlock>& v_block, const uint32_t& pool_index) {
    auto elect_item = elect_info_->GetElectItem(elect_height);
    if (!elect_item) {
        return Status::kError;
    }
    uint32_t replica_idx = elect_item->LocalMember()->index;
    vote_msg->set_replica_idx(replica_idx);
    vote_msg->set_view_block_hash(v_block->hash);    
    vote_msg->set_elect_height(elect_height);
    
    std::string sign_x, sign_y;
    if (crypto(pool_index)->PartialSign(elect_height, GetQCMsgHash(v_block->view, v_block->hash), &sign_x, &sign_y) != Status::kSuccess) {
        ZJC_ERROR("Sign message is error.");
        return Status::kError;
    }
    vote_msg->set_sign_x(sign_x);
    vote_msg->set_sign_x(sign_y);

    std::vector<std::shared_ptr<pools::protobuf::TxMessage>> txs;
    block_wrapper(pool_index)->GetTxsIdempotently(txs);
    for (size_t i = 0; i < txs.size(); i++)
    {
        auto& tx_ptr = *(vote_msg->add_txs());
        tx_ptr = *(txs[i].get());
    }

    return Status::kSuccess;
}

Status HotstuffManager::VerifyVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index,  
    std::shared_ptr<ViewBlock>& view_block) {
    uint32_t replica_idx = vote_msg.replica_idx();
    if (replica_idx > kNodeMax) {
        ZJC_ERROR("replica_idx message is error.");
        return Status::kError;
    }
    // 1、根据hash查找view_block；2、view_block.view <= heighQC.view
    if (!chain(pool_index)->Has(vote_msg.view_block_hash())) {
        // 2. TODO 延迟重试
        ZJC_ERROR("view_block_hash message is not exited.");
        return Status::kError;
    }
    
    chain(pool_index)->Get(vote_msg.view_block_hash(), view_block);
    if (view_block->view <= pacemaker(pool_index)->HighQC()->view) {
        ZJC_ERROR("view message is not exited.");
        return Status::kError;
    }
    
    return Status::kSuccess;
}

Status HotstuffManager::SendTranMsg(const uint32_t pool_index, std::shared_ptr<hotstuff::protobuf::HotstuffMessage>& hotstuff_msg) {
    Status ret = Status::kSuccess;
    auto trans_msg = std::make_shared<transport::TransportMessage>();
    auto& header_msg = trans_msg->header;
    header_msg.set_allocated_hotstuff(hotstuff_msg.get());

    auto leader_rotation = std::make_shared<LeaderRotation>(chain(pool_index), elect_info_);
    auto leader = leader_rotation->GetLeader();
    if (!leader) {
        ZJC_ERROR("Get Leader failed.");
        return Status::kError;
    }
    header_msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(leader->net_id);
    header_msg.set_des_dht_key(dht_key.StrKey());
    header_msg.set_type(common::kHotstuffMessage);
    transport::TcpTransport::Instance()->SetMessageHash(header_msg);
    transport::TcpTransport::Instance()->Send(common::Uint32ToIp(leader->public_ip), leader->public_port, header_msg);
    return ret;
}

void HotstuffManager::ConstructViewBlock(const uint32_t& pool_index, 
    std::shared_ptr<ViewBlock>& view_block,
    std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) {
    auto high_qc = pool_hotstuff_[pool_index].pace_maker->HighQC();
    view_block->parent_hash = (high_qc->view_block_hash);

    auto leader_idx = elect_info_->GetElectItem()->LocalMember()->index;
    view_block->leader_idx = (leader_idx);

    auto pre_v_block = std::make_shared<ViewBlock>();
    chain(pool_index)->Get(high_qc->view_block_hash, pre_v_block);
    auto pre_block = pre_v_block->block;
    auto pb_block = std::make_shared<block::protobuf::Block>();
    block_wrapper(pool_index)->Wrap(pre_block, leader_idx, pb_block, tx_propose);
    view_block->block = (pb_block);

    view_block->qc = high_qc;
    view_block->view = pool_hotstuff_[pool_index].pace_maker->CurView();
    view_block->hash = view_block->DoHash();
}
void HotstuffManager::ConstructPropseMsg(const uint32_t& pool_index, const std::shared_ptr<SyncInfo>& sync_info,
    std::shared_ptr<hotstuff::protobuf::ProposeMsg>& pro_msg) {
    auto new_view_block = std::make_shared<ViewBlock>();
    auto tx_propose = std::make_shared<hotstuff::protobuf::TxPropose>();
    ConstructViewBlock(pool_index, new_view_block, tx_propose);

    auto new_pb_view_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
    ViewBlock2Proto(new_view_block, new_pb_view_block.get());
    pro_msg->set_allocated_view_item(new_pb_view_block.get());
    pro_msg->set_elect_height(elect_info_->GetElectItem()->ElectHeight());
    pro_msg->set_tc_str(sync_info->tc->Serialize());
    pro_msg->set_allocated_tx_propose(tx_propose.get());
}

void HotstuffManager::Propose(const uint32_t& pool_index, const std::shared_ptr<SyncInfo>& sync_info) {
    auto pb_pro_msg = std::make_shared<hotstuff::protobuf::ProposeMsg>();
    ConstructPropseMsg(pool_index, sync_info, pb_pro_msg);


    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& header = msg_ptr->header;
    header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    header.set_type(common::kHotstuffMessage);
    header.set_hop_count(0);
    auto& hotstuff_msg = *header.mutable_hotstuff();
    hotstuff_msg = *(ConstructHotstuffMsg(PROPOSE, pb_pro_msg, nullptr, pool_index));

    ZJC_DEBUG("====0.1 pool: %d, propose, txs size: %lu, view: %lu, hash: %s, qc_view: %lu",
        pool_index,
        hotstuff_msg.pro_msg().tx_propose().txs_size(),
        hotstuff_msg.pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg.pro_msg().view_item().hash()).c_str(),
        pacemaker(pool_index)->HighQC()->view);
    
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    header.set_des_dht_key(dht_key.StrKey());
    assert(header.has_broadcast());
    transport::TcpTransport::Instance()->SetMessageHash(header);
    Status s = crypto(pool_index)->SignMessage(msg_ptr);
    if (s != Status::kSuccess) {
        return;
    }

    ZJC_DEBUG("====0.2 pool: %d, propose, txs size: %lu, view: %lu, hash: %s, qc_view: %lu",
        pool_index,
        hotstuff_msg.pro_msg().tx_propose().txs_size(),
        hotstuff_msg.pro_msg().view_item().view(),
        common::Encode::HexEncode(hotstuff_msg.pro_msg().view_item().hash()).c_str(),
        pacemaker(pool_index)->HighQC()->view);
    
    network::Route::Instance()->Send(msg_ptr);
    return;
}

void HotstuffManager::HandleVoteMsg(const hotstuff::protobuf::VoteMsg& vote_msg, const uint32_t& pool_index) {
    std::shared_ptr<ViewBlock> v_block;
    if (VerifyVoteMsg(vote_msg, pool_index, v_block) != Status::kSuccess) {
        ZJC_ERROR("vote message is error.");
        return;
    }
    // 生成聚合签名，创建qc
    auto elect_height = vote_msg.elect_height();
    auto replica_idx = vote_msg.replica_idx();
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    
    Status ret = crypto(pool_index)->ReconstructAndVerifyThresSign(
            elect_height, v_block->view, v_block->hash, replica_idx, 
            vote_msg.sign_x(), vote_msg.sign_y(), reconstructed_sign);
    
    if (ret != Status::kSuccess) {
        if (ret == Status::kBlsVerifyWaiting) {
            ZJC_INFO("kBlsVerifyWaiting");
            return;
        }
        ZJC_ERROR("ReconstructAndVerify error");
        return;
    }
    
    auto high_view = pacemaker(pool_index)->HighQC()->view;

    auto qc = std::make_shared<QC>();
    Status s = crypto(pool_index)->CreateQC(v_block, reconstructed_sign, qc);
    if (s != Status::kSuccess) {
        return;
    }
    // 切换视图
    pacemaker(pool_index)->AdvanceView(new_sync_info()->WithQC(qc));
    Propose(pool_index, new_sync_info()->WithQC(pacemaker(pool_index)->HighQC()));
    return;
}

void HotstuffManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    ZJC_DEBUG("====1 msg received, timeout: %d", header.has_hotstuff_timeout_proto());

    if (!header.has_hotstuff() && !header.has_hotstuff_timeout_proto()) {
        ZJC_ERROR("transport message is error.");
        return;
    }

    if (header.has_hotstuff()) {
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
        {
            Status s = crypto(hotstuff_msg.pool_index())->VerifyMessage(msg_ptr);
            if (s != Status::kSuccess) {
                return;
            }
            HandleProposeMsg(hotstuff_msg.pro_msg(), hotstuff_msg.pool_index());
            break;
        }
        case VOTE:
            HandleVoteMsg(hotstuff_msg.vote_msg(), hotstuff_msg.pool_index());
            break;
        default:
            ZJC_WARN("consensus message type is error.");
            break;
        }
        return;
    }

    if (header.has_hotstuff_timeout_proto()) {
        auto pool_idx = header.hotstuff_timeout_proto().pool_idx();
        auto pace = pacemaker(pool_idx);
        ZJC_DEBUG("====1.1 msg received, pool_idx: %d", pool_idx);
        pace->OnRemoteTimeout(msg_ptr);
    }

}

}  // namespace consensus

}  // namespace shardora
