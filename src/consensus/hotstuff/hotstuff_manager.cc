#include "hotstuff_manager.h"
#include "leader_rotation.h"

#include <cassert>
#include <chrono>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/agg_crypto.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <consensus/hotstuff/hotstuff.h>
#include <consensus/hotstuff/pacemaker.h>
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

HotstuffManager::HotstuffManager() {}

HotstuffManager::~HotstuffManager() {}

int HotstuffManager::Init(
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
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
    kv_sync_ = kv_sync;
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
#ifdef USE_AGG_BLS
        auto crypto = std::make_shared<AggCrypto>(pool_idx, elect_info_, bls_mgr);
        auto pcrypto = std::make_shared<AggCrypto>(pool_idx, elect_info_, bls_mgr);
#else
        auto crypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
        auto pcrypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
#endif
        auto chain = std::make_shared<ViewBlockChain>(pool_idx, db_, account_mgr_);
        auto leader_rotation = std::make_shared<LeaderRotation>(pool_idx, chain, elect_info_);
        auto pacemaker = std::make_shared<Pacemaker>(
                pool_idx,
                pcrypto,
                leader_rotation,
                std::make_shared<ViewDuration>(
                        pool_idx,
                        ViewDurationSampleSize,
                        ViewDurationStartTimeoutMs,
                        ViewDurationMaxTimeoutMs,
                        ViewDurationMultiplier));
        auto acceptor = std::make_shared<BlockAcceptor>(
                pool_idx, security_ptr, account_mgr, elect_info_, vss_mgr,
                contract_mgr, db, gas_prepayment, pool_mgr, block_mgr,
                tm_block_mgr, elect_mgr, new_block_cache_callback);
        auto wrapper = std::make_shared<BlockWrapper>(
                pool_idx, pool_mgr, tm_block_mgr, block_mgr, elect_info_);
        
        pool_hotstuff_[pool_idx] = std::make_shared<Hotstuff>(
                kv_sync, pool_idx, leader_rotation, chain,
                acceptor, wrapper, pacemaker, crypto, elect_info_, db_);
        pool_hotstuff_[pool_idx]->Init();
    }

    RegisterCreateTxCallbacks();
    network::Route::Instance()->RegisterMessage(common::kHotstuffMessage,
        std::bind(&HotstuffManager::HandleMessage, this, std::placeholders::_1));
    network::Route::Instance()->RegisterMessage(common::kHotstuffTimeoutMessage,
        std::bind(&HotstuffManager::HandleMessage, this, std::placeholders::_1));
    transport::Processor::Instance()->RegisterProcessor(
        common::kPacemakerTimerMessage,
        std::bind(&HotstuffManager::HandleTimerMessage, this, std::placeholders::_1));    
    return kConsensusSuccess;
}

// start chained hotstuff
Status HotstuffManager::Start() {
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        Status s = hotstuff(pool_idx)->Start();
        if (s != Status::kSuccess) {
            return s;
        }
    }
    return Status::kSuccess;
}


int HotstuffManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

int HotstuffManager::VerifySyncedViewBlock(const view_block::protobuf::ViewBlockItem& pb_vblock) {
    if (!pb_vblock.has_qc()) {
        return -1;
    }

    // 由于验签很占资源，再检查一下数据库，避免重复同步
    if (prefix_db_->HasViewBlockInfo(pb_vblock.hash())) {
        ZJC_DEBUG("already stored, %lu_%lu_%lu, hash: %s",
            pb_vblock.network_id(),
            pb_vblock.pool_index(),
            pb_vblock.block_info().height(),
            common::Encode::HexEncode(pb_vblock.hash()).c_str());
        return 0;
    }
    
    auto s = VerifyViewBlockWithCommitQC(pb_vblock);
    if (s != Status::kSuccess) {
        return s == Status::kElectItemNotFound ? 1 : -1;
    }
    return 0;
}

// 验证有 qc 的 view block
Status HotstuffManager::VerifyViewBlockWithCommitQC(const view_block::protobuf::ViewBlockItem& vblock) {
    if (!vblock.has_qc() || !vblock.has_block_info()) {
        ZJC_ERROR("vblock is not valid, blockview: %lu, qcview: %lu");
        return Status::kInvalidArgument;
    }

    if (vblock.block_info().height() == 0) {
        return Status::kSuccess;
    }

    // if (view_block_hash != vblock.hash()) {
    //     ZJC_ERROR("hash is not same with qc, block: %s, commit_hash: %s",
    //         common::Encode::HexEncode(view_block_hash).c_str(),
    //         common::Encode::HexEncode(vblock.hash()).c_str());
    //     assert(false);
    //     return Status::kInvalidArgument;
    // }
#ifdef USE_AGG_BLS
    AggregateSignature agg_sig;
    if (!agg_sig.LoadFromProto(vblock.qc().agg_sig())) {
        return Status::kError;
    }

    auto view_block_hash = GetQCMsgHash(vblock.qc());
    auto hf = hotstuff(vblock.pool_index());
    
    Status s = hf->crypto()->Verify(
            agg_sig,
            view_block_hash,
            vblock.network_id(), 
            vblock.elect_height());
    if (s != Status::kSuccess) {
        ZJC_ERROR("qc verify failed, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.view(), vblock.view(),
            vblock.network_id(),
            vblock.pool_index(),
            vblock.block_info().height(),
            vblock.elect_height(),
            network::kRootCongressNetworkId,
            vblock.network_id(),
            vblock.elect_height());
        return s;
    }

    ZJC_DEBUG("qc verify success, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.view(), vblock.view(),
            vblock.network_id(),
            vblock.pool_index(),
            vblock.block_info().height(),
            vblock.elect_height(),
            network::kRootCongressNetworkId,
            vblock.network_id(),
            vblock.elect_height());    
#else
    libff::alt_bn128_G1 sign = libff::alt_bn128_G1::zero();
    try {
        if (vblock.qc().sign_x() != "") {
            sign.X = libff::alt_bn128_Fq(vblock.qc().sign_x().c_str());
        }
        
        if (vblock.qc().sign_y() != "") {
            sign.Y = libff::alt_bn128_Fq(vblock.qc().sign_y().c_str());
        }

        if (vblock.qc().sign_z() != "") {
            sign.Z = libff::alt_bn128_Fq(vblock.qc().sign_z().c_str());
        }
    } catch (...) {
        return Status::kInvalidArgument;
    }

    auto view_block_hash = GetQCMsgHash(vblock.qc());
    auto hf = hotstuff(vblock.pool_index());
    Status s = hf->crypto()->VerifyThresSign(
        vblock.network_id(), 
        vblock.elect_height(), 
        view_block_hash, 
        sign);
    if (s != Status::kSuccess) {
        ZJC_ERROR("qc verify failed, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.view(), vblock.view(),
            vblock.network_id(),
            vblock.pool_index(),
            vblock.block_info().height(),
            vblock.elect_height(),
            network::kRootCongressNetworkId,
            vblock.network_id(),
            vblock.elect_height());
        return s;
    }

    ZJC_DEBUG("qc verify success, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.view(), vblock.view(),
            vblock.network_id(),
            vblock.pool_index(),
            vblock.block_info().height(),
            vblock.elect_height(),
            network::kRootCongressNetworkId,
            vblock.network_id(),
            vblock.elect_height());
#endif    
    return Status::kSuccess;
}

void HotstuffManager::OnNewElectBlock(uint64_t block_tm_ms, uint32_t sharding_id, uint64_t elect_height,
    common::MembersPtr& members, const libff::alt_bn128_G2& common_pk, const libff::alt_bn128_Fr& sec_key) {        
    elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
}

void HotstuffManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = msg_ptr->header;
    if (header.has_hotstuff_timeout_proto() ||
        (header.has_hotstuff() && header.hotstuff().type() == VOTE) ||
        (header.has_hotstuff() && header.hotstuff().type() == PRE_RESET_TIMER)) {
        dht::DhtKeyManager dht_key(
            msg_ptr->header.src_sharding_id(),
            security_ptr_->GetAddress());
        if (msg_ptr->header.des_dht_key() != dht_key.StrKey()) {
            network::Route::Instance()->Send(msg_ptr);
            ZJC_DEBUG("hotstuff message resend to leader by latest node net: %u, "
                "id: %s, des dht: %s, local: %s, hash64: %lu",
                msg_ptr->header.src_sharding_id(), 
                common::Encode::HexEncode(security_ptr_->GetAddress()).c_str(),
                common::Encode::HexEncode(msg_ptr->header.des_dht_key()).c_str(),
                common::Encode::HexEncode(dht_key.StrKey()).c_str(),
                header.hash64());
            return;
        }
    }

    if (!header.has_hotstuff() && !header.has_hotstuff_timeout_proto()) {
        ZJC_ERROR("transport message is error.");
        return;
    }

    ZJC_DEBUG("hotstuff message coming from: %s:%d, hash64: %lu, type: %d", 
        msg_ptr->conn ? msg_ptr->conn->PeerIp().c_str() : "", msg_ptr->conn ? msg_ptr->conn->PeerPort() : 0, 
        header.hash64(), header.hotstuff().type());
    if (header.has_hotstuff()) {
        auto& hotstuff_msg = header.hotstuff();
        if (hotstuff_msg.net_id() != common::GlobalInfo::Instance()->network_id()) {
            ZJC_ERROR("net_id is error.");
            return;
        }
        if (hotstuff_msg.pool_index() >= common::kInvalidPoolIndex) {
            ZJC_ERROR("pool index invalid[%d]!", hotstuff_msg.pool_index());
            return;
        }
        switch (hotstuff_msg.type())
        {
        case PROPOSE:
        {
            ADD_DEBUG_PROCESS_TIMESTAMP();
            Status s = crypto(hotstuff_msg.pool_index())->VerifyMessage(msg_ptr);
            if (s != Status::kSuccess) {
                return;
            }
            hotstuff(hotstuff_msg.pool_index())->HandleProposeMsg(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            break;
        }
        case VOTE:
            ADD_DEBUG_PROCESS_TIMESTAMP();
            hotstuff(hotstuff_msg.pool_index())->HandleVoteMsg(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            break;
        case NEWVIEW: // 接收 tc 和 qc
            ADD_DEBUG_PROCESS_TIMESTAMP();
            hotstuff(hotstuff_msg.pool_index())->HandleNewViewMsg(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            break;
        case PRE_RESET_TIMER:
            ADD_DEBUG_PROCESS_TIMESTAMP();
            hotstuff(hotstuff_msg.pool_index())->HandlePreResetTimerMsg(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            break;
        case RESET_TIMER:
            hotstuff(hotstuff_msg.pool_index())->HandleResetTimerMsg(header);
            break;                      
        default:
            ZJC_WARN("consensus message type is error.");
            break;
        }
        return;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (header.has_hotstuff_timeout_proto()) {
        auto pool_idx = header.hotstuff_timeout_proto().pool_idx();
        pacemaker(pool_idx)->OnRemoteTimeout(msg_ptr);
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

void HotstuffManager::HandleTimerMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    account_mgr_->GetAccountInfo("");
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_index) {
            pacemaker(pool_idx)->HandleTimerMessage(msg_ptr);
            
            bool has_user_tx = false;
            bool has_system_tx = false;
            
            auto gid_valid_func = [&](const std::string& gid) -> bool {
                return pool_hotstuff_[pool_idx]->view_block_chain()->CheckTxGidValid(
                        gid, 
                        pacemaker(pool_idx)->HighQC()->view_block_hash());
            };

            if (now_tm_ms >= prev_check_timer_single_tm_ms_[pool_idx] + 1000lu) {
                prev_check_timer_single_tm_ms_[pool_idx] = now_tm_ms;
                has_system_tx = block_wrapper(pool_idx)->HasSingleTx(gid_valid_func);
            }

            ADD_DEBUG_PROCESS_TIMESTAMP();
            pools_mgr_->PopTxs(pool_idx, false, &has_user_tx, &has_system_tx);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            pools_mgr_->CheckTimeoutTx(pool_idx);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            hotstuff(pool_idx)->TryRecoverFromStuck(has_user_tx, has_system_tx);
            ADD_DEBUG_PROCESS_TIMESTAMP();
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // 打印总 tps
    double tps = 0;
    if (now_tm_ms >= prev_handler_timer_tm_ms_ + 3000lu) {
        prev_handler_timer_tm_ms_ = now_tm_ms;
        for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
            auto pool_tps = hotstuff(pool_idx)->acceptor()->Tps();
            if (prev_tps_[pool_idx] != pool_tps) {
                tps += pool_tps;
                prev_tps_[pool_idx] = pool_tps;
            }
        }

        if (tps >= 0.000001) {
            ZJC_ERROR("total tps: %.2f, net: %d", tps, common::GlobalInfo::Instance()->network_id());
        }
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

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
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kPoolStatisticTag,
        std::bind(&HotstuffManager::CreatePoolStatisticTagTx, this, std::placeholders::_1));
    block_mgr_->SetCreateToTxFunction(
        std::bind(&HotstuffManager::CreateToTx, this, std::placeholders::_1));
    block_mgr_->SetCreateStatisticTxFunction(
        std::bind(&HotstuffManager::CreateStatisticTx, this, std::placeholders::_1));
    block_mgr_->SetCreateElectTxFunction(
        std::bind(&HotstuffManager::CreateElectTx, this, std::placeholders::_1));
    tm_block_mgr_->SetCreateTmTxFunction(
        std::bind(&HotstuffManager::CreateTimeblockTx, this, std::placeholders::_1));
}

}  // namespace consensus

}  // namespace shardora
