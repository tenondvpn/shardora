#include "hotstuff_manager.h"
#include "leader_rotation.h"

#include <cassert>
#include <chrono>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/block_wrapper.h>
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

HotstuffManager::HotstuffManager() {}

HotstuffManager::~HotstuffManager() {}

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

        auto crypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
        auto chain = std::make_shared<ViewBlockChain>();
        auto leader_rotation = std::make_shared<LeaderRotation>(chain, elect_info_);
        auto pacemaker = std::make_shared<Pacemaker>(pool_idx, crypto, leader_rotation, std::make_shared<ViewDuration>());
        auto acceptor = std::make_shared<BlockAcceptor>(
                pool_idx, security_ptr, account_mgr, elect_info_, vss_mgr,
                contract_mgr, db, gas_prepayment, pool_mgr, block_mgr,
                tm_block_mgr, new_block_cache_callback);
        auto wrapper = std::make_shared<BlockWrapper>(
                pool_idx, pool_mgr, tm_block_mgr, block_mgr, elect_info_);
        
        auto hf = Hotstuff(pool_idx, leader_rotation, chain,
            acceptor, wrapper, pacemaker, crypto, elect_info_);
        hf.Init(db_);
        pool_hotstuff_.emplace(pool_idx, std::move(hf));
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

void HotstuffManager::OnNewElectBlock(uint64_t block_tm_ms, uint32_t sharding_id, uint64_t elect_height,
    common::MembersPtr& members, const libff::alt_bn128_G2& common_pk, const libff::alt_bn128_Fr& sec_key) {        
        elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
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
            hotstuff(hotstuff_msg.pool_index())->HandleProposeMsg(hotstuff_msg.pro_msg());
            break;
        }
        case VOTE:
            hotstuff(hotstuff_msg.pool_index())->HandleVoteMsg(hotstuff_msg.vote_msg());
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

}  // namespace consensus

}  // namespace shardora
