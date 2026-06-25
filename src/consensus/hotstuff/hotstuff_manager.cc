#include "hotstuff_manager.h"

#include <cassert>
#include <chrono>

#include <libbls/tools/utils.h>

#include "bls/bls_utils.h"
#include "bls/bls_manager.h"
#include "bls/bls_sign.h"
#include "common/defer.h"
#include "common/encode.h"
#include "common/hash.h"
#include "common/log.h"
#include "common/global_info.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "consensus/hotstuff/block_acceptor.h"
#include "consensus/hotstuff/block_wrapper.h"
#include "consensus/hotstuff/hotstuff.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "db/db.h"
#include "dht/base_dht.h"
#include "elect/elect_manager.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "pools/tx_pool_manager.h"
#include "protos/pools.pb.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "security/eth_verify.h"
#include "transport/processor.h"
#include "types.h"

namespace shardora {

namespace consensus {

HotstuffManager::HotstuffManager() {}

HotstuffManager::~HotstuffManager() {
    destroy_ = true;
    // if (pop_message_thread_) {
    //     pop_message_thread_->join();
    // }
}

int HotstuffManager::Init(
        std::shared_ptr<sync::KeyValueSync>& kv_sync,
        std::shared_ptr<contract::ContractManager>& contract_mgr,
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
    // pop_message_thread_ = std::make_shared<std::thread>(
    //     &HotstuffManager::PopPoolsMessage, 
    //     this);

    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        auto crypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
        auto pcrypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
        auto chain = std::make_shared<ViewBlockChain>();
        pools::protobuf::PoolLatestInfo pool_latest_info;
        InitLatestInfo(pool_latest_info, pool_idx);
        auto pacemaker = std::make_shared<Pacemaker>(
            pool_idx,
            pcrypto,
            std::make_shared<ViewDuration>(
                pool_idx,
                ViewDurationSampleSize,
                ViewDurationStartTimeoutMs,
                ViewDurationMaxTimeoutMs,
                ViewDurationMultiplier),
            std::bind(&ViewBlockChain::HighQC, chain),
            std::bind(&ViewBlockChain::UpdateHighViewBlock, chain, std::placeholders::_1),
            pool_latest_info);
        auto acceptor = std::make_shared<BlockAcceptor>();
        chain->Init(
            kLocalChain,
            pool_idx, db_, block_mgr_, account_mgr_, 
            kv_sync, acceptor, pool_mgr, new_block_cache_callback);
        acceptor->Init(
            pool_idx, security_ptr, account_mgr, elect_info_, vss_mgr,
            contract_mgr, db, pool_mgr, block_mgr,
            tm_block_mgr, elect_mgr, chain, bls_mgr_);
        auto wrapper = std::make_shared<BlockWrapper>(
                pool_idx, pool_mgr, tm_block_mgr, block_mgr, bls_mgr, elect_info_);
        pool_hotstuff_[pool_idx] = std::make_shared<Hotstuff>(
            block_mgr_,
            *this,
            kv_sync, pool_idx, chain,
            acceptor, wrapper, pacemaker, crypto, elect_info_, db_, tm_block_mgr,
            new_block_cache_callback);
        pool_hotstuff_[pool_idx]->Init();
    }

    SHARDORA_WARN("success init hotstuff manager!");
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

// start chained-hotstuff
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
    auto begin_ms = common::TimeUtils::TimestampMs();
    defer({
        auto cost_ms = common::TimeUtils::TimestampMs() - begin_ms;
        SHARDORA_DEBUG("VerifySyncedViewBlock cost: %lu ms, block: %u_%u_%lu_%lu",
            cost_ms,
            pb_vblock.has_qc() ? pb_vblock.qc().network_id() : 0,
            pb_vblock.has_qc() ? pb_vblock.qc().pool_index() : 0,
            pb_vblock.has_block_info() ? pb_vblock.block_info().height() : 0,
            pb_vblock.has_qc() ? pb_vblock.qc().view() : 0);
    });

    if (!pb_vblock.has_qc()) {
        return -1;
    }

    // Since signature verification is resource-intensive, check the database again to avoid repeated synchronization.
    if (prefix_db_->BlockExists(pb_vblock.qc().view_block_hash())) {
        SHARDORA_DEBUG("already stored, %lu_%lu_%lu, hash: %s",
            pb_vblock.qc().network_id(),
            pb_vblock.qc().pool_index(),
            pb_vblock.block_info().height(),
            common::Encode::HexEncode(pb_vblock.qc().view_block_hash()).c_str());
        return 2;
    }
    
    auto s = VerifyViewBlockWithCommitQC(pb_vblock);
    if (s != Status::kSuccess) {
        return s == Status::kElectItemNotFound ? 1 : -1;
    }
    
    return 0;
}

// Verify view block with commit qc
Status HotstuffManager::VerifyViewBlockWithCommitQC(const view_block::protobuf::ViewBlockItem& vblock) {
    if (!vblock.has_qc() || !vblock.has_block_info()) {
        SHARDORA_ERROR("vblock is not valid, blockview: %lu, qcview: %lu");
        return Status::kInvalidArgument;
    }

    if (vblock.block_info().height() == 0) {
        return Status::kSuccess;
    }

    // if (view_block_hash != vblock.qc().view_block_hash()) {
    //     SHARDORA_ERROR("hash is not same with qc, block: %s, commit_hash: %s",
    //         common::Encode::HexEncode(view_block_hash).c_str(),
    //         common::Encode::HexEncode(vblock.qc().view_block_hash()).c_str());
    //     //assert(false);
    //     return Status::kInvalidArgument;
    // }

    libff::alt_bn128_G1 sign = libff::alt_bn128_G1::zero();
    try {
        if (vblock.qc().sign_x() != "") {
            sign.X = libff::alt_bn128_Fq(vblock.qc().sign_x().c_str());
        }
        
        if (vblock.qc().sign_y() != "") {
            sign.Y = libff::alt_bn128_Fq(vblock.qc().sign_y().c_str());
        }

        sign.Z = libff::alt_bn128_Fq::one();
    } catch (...) {
        return Status::kInvalidArgument;
    }

    auto view_block_hash = GetQCMsgHash(vblock.qc());
    SHARDORA_DEBUG("view block hash: %s, get hash: %s, now check bls sign: x: %s, y: %s, z: %s", 
        common::Encode::HexEncode(vblock.qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block_hash).c_str(),
        vblock.qc().sign_x().c_str(),
        vblock.qc().sign_y().c_str(),
        vblock.qc().sign_z().c_str());
    auto hf = hotstuff(vblock.qc().pool_index());
    Status s = hf->crypto()->VerifyThresSign(
        vblock.qc().network_id(), 
        vblock.qc().elect_height(), 
        view_block_hash, 
        sign);
    if (s != Status::kSuccess) {
        SHARDORA_ERROR("qc verify failed, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            (int32_t)s, vblock.qc().view(), vblock.qc().view(),
            vblock.qc().network_id(),
            vblock.qc().pool_index(),
            vblock.block_info().height(),
            vblock.qc().elect_height(),
            network::kRootCongressNetworkId,
            vblock.qc().network_id(),
            vblock.qc().elect_height());
        return s;
    }

    SHARDORA_DEBUG("qc verify success, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            (int32_t)s, vblock.qc().view(), vblock.qc().view(),
            vblock.qc().network_id(),
            vblock.qc().pool_index(),
            vblock.block_info().height(),
            vblock.qc().elect_height(),
            network::kRootCongressNetworkId,
            vblock.qc().network_id(),
            vblock.qc().elect_height());
    return Status::kSuccess;
}

void HotstuffManager::OnNewElectBlock(
        uint32_t sharding_id, 
        uint64_t elect_height,
        common::MembersPtr& members, 
        const libff::alt_bn128_G2& common_pk, 
        const libff::alt_bn128_Fr& sec_key) {        
    elect_info_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; i++) {
        pool_hotstuff_[i]->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
    }
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
            SHARDORA_DEBUG("hotstuff message resend to leader by latest node net: %u, "
                "id: %s, des dht: %s, local: %s, hash64: %lu, "
                "header.has_hotstuff_timeout_proto(): %d, type: %d",
                msg_ptr->header.src_sharding_id(), 
                common::Encode::HexEncode(security_ptr_->GetAddress()).c_str(),
                common::Encode::HexEncode(msg_ptr->header.des_dht_key()).c_str(),
                common::Encode::HexEncode(dht_key.StrKey()).c_str(),
                header.hash64(),
                header.has_hotstuff_timeout_proto(),
                header.has_hotstuff() && header.hotstuff().type());
            return;
        }
    }

    if (!header.has_hotstuff() && !header.has_hotstuff_timeout_proto()) {
        SHARDORA_ERROR("transport message is error.");
        return;
    }

    SHARDORA_DEBUG("hotstuff message coming from: %s:%d, hash64: %lu, has hoststuff: %d, type: %d", 
        msg_ptr->conn ? msg_ptr->conn->PeerIp().c_str() : "", msg_ptr->conn ? msg_ptr->conn->PeerPort() : 0, 
        header.hash64(), header.has_hotstuff(), header.hotstuff().type());
    if (header.has_hotstuff()) {
        auto& hotstuff_msg = header.hotstuff();
        if (hotstuff_msg.net_id() != common::GlobalInfo::Instance()->network_id()) {
            SHARDORA_ERROR("net_id is error.");
            return;
        }

        if (hotstuff_msg.pool_index() >= common::kInvalidPoolIndex) {
            SHARDORA_ERROR("pool index invalid[%d]!", hotstuff_msg.pool_index());
            return;
        }

        switch (hotstuff_msg.type())  {
            case PROPOSE: {
                ADD_DEBUG_PROCESS_TIMESTAMP();
                Status s = crypto(hotstuff_msg.pool_index())->VerifyMessage(msg_ptr);
                if (s != Status::kSuccess) {
                    SHARDORA_DEBUG("verify message failed: hash64: %lu", header.hash64());
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
            case PRE_RESET_TIMER:
                ADD_DEBUG_PROCESS_TIMESTAMP();
                hotstuff(hotstuff_msg.pool_index())->HandlePreResetTimerMsg(msg_ptr);
                ADD_DEBUG_PROCESS_TIMESTAMP();
                break;
            default:
                SHARDORA_WARN("consensus message type is error.");
                break;
        }
        return;
    }

    // ADD_DEBUG_PROCESS_TIMESTAMP();
    // if (header.has_hotstuff_timeout_proto()) {
    //     auto pool_idx = header.hotstuff_timeout_proto().pool_idx();
    //     pacemaker(pool_idx)->OnRemoteTimeout(msg_ptr);
    // }
    // ADD_DEBUG_PROCESS_TIMESTAMP();
}

void HotstuffManager::HandleTimerMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        msg_ptr->times_idx = 0;
        if (transport::TcpTransport::Instance()->GetThreadIndexWithPool(pool_idx) == thread_index) {
            bool has_user_tx = false;
            bool has_system_tx = false;
            ADD_DEBUG_PROCESS_TIMESTAMP();
            auto tx_valid_func = [&](
                    const address::protobuf::AddressInfo& addr_info, 
                    const pools::protobuf::TxMessage& tx_info,
                    uint64_t* now_nonce) -> int {
                auto latest_block = pool_hotstuff_[pool_idx]->view_block_chain()->HighViewBlock();
                if (!latest_block) {
                    return -1;
                }
                
                return CheckTransactionValid(
                    latest_block->qc().view_block_hash(), 
                    pool_hotstuff_[pool_idx]->view_block_chain(), 
                    pools_mgr_,
                    addr_info, 
                    tx_info,
                    now_nonce);
            };

            if (now_tm_ms >= prev_check_timer_single_tm_ms_[pool_idx] + 1000lu) {
                prev_check_timer_single_tm_ms_[pool_idx] = now_tm_ms;
                has_system_tx = block_wrapper(pool_idx)->HasSingleTx(msg_ptr, tx_valid_func);
            }

            ADD_DEBUG_PROCESS_TIMESTAMP();
            hotstuff(pool_idx)->TryRecoverFromStuck(
                msg_ptr, 
                pools_mgr_->all_tx_size(pool_idx) > 0, 
                has_system_tx);
            ADD_DEBUG_PROCESS_TIMESTAMP();
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
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

        if (tps >= 0.000001) { // Print total tps
            SHARDORA_WARN("tps: %.2f", tps);
        }
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
    PopPoolsMessage();
    ADD_DEBUG_PROCESS_TIMESTAMP();
}


void HotstuffManager::PopPoolsMessage() {
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    auto consensus_tx_count = 0;
    while (!destroy_) {
        if (consensus_add_tx_msgs_[thread_index].empty()) {
            break;
        }

        auto msg_ptr = consensus_add_tx_msgs_[thread_index].front();
        consensus_add_tx_msgs_[thread_index].pop();
        const google::protobuf::RepeatedPtrField<shardora::pools::protobuf::TxMessage>* txs_ptr = nullptr;
        if (msg_ptr->header.hotstuff().has_pre_reset_timer_msg()) {
            txs_ptr = &msg_ptr->header.hotstuff().pre_reset_timer_msg().txs();
        } else {
            auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
            txs_ptr = &vote_msg.txs();
        }

        auto& txs = *txs_ptr;
        consensus_tx_count += txs.size();
        SHARDORA_DEBUG("tps success handle message hash64: %lu, tx size: %d", msg_ptr->header.hash64(), txs.size());
        for (uint32_t i = 0; i < uint32_t(txs.size()); i++) {
            auto* tx = &txs[i];
            if (!pools::IsUserTransaction(tx->step())) {
                continue;
            }
            
            auto from_id = security_ptr_->GetAddressWithPublicKey(tx->pubkey());
            uint32_t pool_index = common::kInvalidPoolIndex;
            protos::AddressInfoPtr address_info = nullptr;
            if (tx->step() == pools::protobuf::kContractExcute || 
                    tx->step() == pools::protobuf::kContractRefund) {
                pool_index = common::GetAddressPoolIndex(tx->to());
                auto prefund_id = tx->to() + from_id;
                address_info = pool_hotstuff_[pool_index]->view_block_chain()->ChainGetAccountInfo(prefund_id);
            } else {
                pool_index = common::GetAddressPoolIndex(tx->to());
                address_info = pool_hotstuff_[pool_index]->view_block_chain()->ChainGetAccountInfo(from_id);
            }
    
            if (!address_info) {
                SHARDORA_WARN("get address failed nonce: %lu", tx->nonce());
                continue;
            }

            if (address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
                SHARDORA_WARN("sharding error: %d, %d",
                    address_info->sharding_id(),
                    common::GlobalInfo::Instance()->network_id());
                SHARDORA_ERROR("failed add tx. %s", common::Encode::HexEncode(address_info->addr()).c_str());
                continue;
            }
            
            auto tx_hash = pools::GetTxMessageHash(*tx);
            if (!pool_hotstuff_[address_info->pool_index()]->acceptor()->TxHashVerified(tx_hash)) {
                continue;
            }

            std::string contract_prefund_id;
            pools::TxItemPtr tx_ptr = nullptr;
            switch (tx->step()) {
            case pools::protobuf::kNormalFrom:
                tx_ptr = std::make_shared<consensus::FromTxItem>(
                        msg_ptr, i, account_mgr_, security_ptr_, address_info);
                // ADD_TX_DEBUG_INFO((const_cast<pools::protobuf::TxMessage*>(tx)));
                break;
            case pools::protobuf::kCreateContract:
                tx_ptr = std::make_shared<consensus::ContractUserCreateCall>(
                        contract_mgr_, 
                        db_, 
                        msg_ptr, i, 
                        account_mgr_, 
                        security_ptr_, 
                        address_info);
                contract_prefund_id = tx->to() + from_id;
                break;
            case pools::protobuf::kCreateLibrary:
                tx_ptr = std::make_shared<consensus::CreateLibrary>(
                        msg_ptr, i, 
                        account_mgr_, 
                        security_ptr_, 
                        address_info);
                contract_prefund_id = tx->to() + from_id;
                break;
            case pools::protobuf::kContractExcute:
                tx_ptr = std::make_shared<consensus::ContractCall>(
                        contract_mgr_, 
                        db_, 
                        msg_ptr, i,
                        account_mgr_, 
                        security_ptr_, 
                        address_info);
                contract_prefund_id = tx->to() + from_id;
                break;
            case pools::protobuf::kContractGasPrefund:
                tx_ptr = std::make_shared<consensus::ContractPrefund>(
                        db_, 
                        msg_ptr, i,
                        account_mgr_, 
                        security_ptr_, 
                        address_info);
                contract_prefund_id = tx->to() + from_id;
                break;
            case pools::protobuf::kContractRefund:
                tx_ptr = std::make_shared<consensus::ContractRefund>(
                        db_, 
                        msg_ptr, i,
                        account_mgr_, 
                        security_ptr_, 
                        address_info);
                contract_prefund_id = tx->to() + from_id;
                break;
            case pools::protobuf::kJoinElect:
            {
                tx_ptr = std::make_shared<consensus::JoinElectTxItem>(
                    msg_ptr, i, 
                    account_mgr_, 
                    security_ptr_, 
                    prefix_db_, 
                    elect_mgr_, 
                    address_info,
                    (*tx).pubkey());
                break;
            }
            default:
                break;
            }
            
            if (tx_ptr != nullptr) {
                // ETH-format tx: signature already verified in http_handler.cc
                // during RLP decoding and signature recovery. Skip verification here.
                if (tx_ptr->tx_info->has_eth_raw_tx() && !tx_ptr->tx_info->eth_raw_tx().empty()) {
                    pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                } else if (tx_ptr->tx_info->pubkey().size() == 64u) {
                    security::GmSsl gmssl;
                    if (gmssl.Verify(
                            tx_hash,
                            tx_ptr->tx_info->pubkey(),
                            tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                        SHARDORA_WARN("GmSsl verify failed in PopPoolsMessage, addr=%s",
                            common::Encode::HexEncode(address_info->addr()).c_str());
                    } else {
                        pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                    }
                } else if (tx_ptr->tx_info->pubkey().size() > 128u) {
                    security::Oqs oqs;
                    if (oqs.Verify(
                            tx_hash,
                            tx_ptr->tx_info->pubkey(),
                            tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                        SHARDORA_WARN("Oqs verify failed in PopPoolsMessage, addr=%s",
                            common::Encode::HexEncode(address_info->addr()).c_str());
                    } else {
                        pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                    }
                } else {
                    if (security_ptr_->Verify(
                            tx_hash,
                            tx_ptr->tx_info->pubkey(),
                            tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                        SHARDORA_WARN("ECDSA verify failed in PopPoolsMessage, addr=%s, pk_len=%zu",
                            common::Encode::HexEncode(address_info->addr()).c_str(),
                            tx_ptr->tx_info->pubkey().size());
                    } else {
                        pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                    }
                }
            }
        }
    }

    if (consensus_tx_count > 0) {
        SHARDORA_DEBUG("tps success add consensus_tx_count: %lu", consensus_tx_count);
    }
}

void HotstuffManager::InitLatestInfo(pools::protobuf::PoolLatestInfo& pool_info, uint32_t pool_index) {
    uint32_t network_id = common::GlobalInfo::Instance()->network_id();
    if (network_id == common::kInvalidUint32) {
        return;
    }

    if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id < network::kConsensusWaitingShardEndNetworkId) {
        network_id -= network::kConsensusWaitingShardOffset;
    }

    if (!prefix_db_->GetLatestPoolInfo(
            network_id,
            pool_index,
            &pool_info)) {
        SHARDORA_ERROR("failed get pool latest info: %d", pool_index);
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
        pools::protobuf::kRootCreateAddress,
        std::bind(&HotstuffManager::CreateRootToTxItem, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kCreateContract,
        std::bind(&HotstuffManager::CreateContractUserCreateCallTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractGasPrefund,
        std::bind(&HotstuffManager::CreateContractPrefundTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kContractRefund,
        std::bind(&HotstuffManager::CreateContractRefundTx, this, std::placeholders::_1));
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
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kStatistic,
        std::bind(&HotstuffManager::CreateStatisticTx, this, std::placeholders::_1));
    pools_mgr_->RegisterCreateTxFunction(
        pools::protobuf::kConsensusRootElectShard,
        std::bind(&HotstuffManager::CreateElectTx, this, std::placeholders::_1));
    block_mgr_->SetCreateToTxFunction(
        std::bind(&HotstuffManager::CreateToTx, this, std::placeholders::_1));
    tm_block_mgr_->SetCreateTmTxFunction(
        std::bind(&HotstuffManager::CreateTimeblockTx, this, std::placeholders::_1));
}

}  // namespace consensus

}  // namespace shardora
