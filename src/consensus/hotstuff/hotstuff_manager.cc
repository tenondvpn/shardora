#include "hotstuff_manager.h"
#include "leader_rotation.h"

#include <cassert>
#include <chrono>

#include <libbls/tools/utils.h>

#include "bls/bls_utils.h"
#include "bls/bls_manager.h"
#include "bls/bls_sign.h"
#include "common/encode.h"
#include "common/hash.h"
#include "common/log.h"
#include "common/global_info.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "consensus/hotstuff/agg_crypto.h"
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
#include "transport/processor.h"
#include "types.h"

namespace shardora {

namespace consensus {

HotstuffManager::HotstuffManager() {}

HotstuffManager::~HotstuffManager() {
    destroy_ = true;
    if (pop_message_thread_) {
        pop_message_thread_->join();
    }
}

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
    pop_message_thread_ = std::make_shared<std::thread>(
        &HotstuffManager::PopPoolsMessage, 
        this);

    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
#ifdef USE_AGG_BLS
        auto crypto = std::make_shared<AggCrypto>(pool_idx, elect_info_, bls_mgr);
        auto pcrypto = std::make_shared<AggCrypto>(pool_idx, elect_info_, bls_mgr);
#else
        auto crypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
        auto pcrypto = std::make_shared<Crypto>(pool_idx, elect_info_, bls_mgr);
#endif
        auto chain = std::make_shared<ViewBlockChain>();
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
                        ViewDurationMultiplier),
                std::bind(&ViewBlockChain::HighQC, chain),
                std::bind(&ViewBlockChain::UpdateHighViewBlock, chain, std::placeholders::_1));
        auto acceptor = std::make_shared<BlockAcceptor>();
        chain->Init(
            pool_idx, db_, block_mgr_, account_mgr_, 
            kv_sync, acceptor, pool_mgr, new_block_cache_callback);
        acceptor->Init(
            pool_idx, security_ptr, account_mgr, elect_info_, vss_mgr,
            contract_mgr, db, gas_prepayment, pool_mgr, block_mgr,
            tm_block_mgr, elect_mgr, chain);
        auto wrapper = std::make_shared<BlockWrapper>(
                pool_idx, pool_mgr, tm_block_mgr, block_mgr, elect_info_);
        pool_hotstuff_[pool_idx] = std::make_shared<Hotstuff>(
                *this,
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
    if (prefix_db_->BlockExists(pb_vblock.qc().view_block_hash())) {
        ZJC_DEBUG("already stored, %lu_%lu_%lu, hash: %s",
            pb_vblock.qc().network_id(),
            pb_vblock.qc().pool_index(),
            pb_vblock.block_info().height(),
            common::Encode::HexEncode(pb_vblock.qc().view_block_hash()).c_str());
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

    // if (view_block_hash != vblock.qc().view_block_hash()) {
    //     ZJC_ERROR("hash is not same with qc, block: %s, commit_hash: %s",
    //         common::Encode::HexEncode(view_block_hash).c_str(),
    //         common::Encode::HexEncode(vblock.qc().view_block_hash()).c_str());
    //     assert(false);
    //     return Status::kInvalidArgument;
    // }
#ifdef USE_AGG_BLS
    AggregateSignature agg_sig;
    if (!agg_sig.LoadFromProto(vblock.qc().agg_sig())) {
        return Status::kError;
    }

    auto view_block_hash = GetQCMsgHash(vblock.qc());
    auto hf = hotstuff(vblock.qc().pool_index());
    
    Status s = hf->crypto()->Verify(
            agg_sig,
            view_block_hash,
            vblock.qc().network_id(), 
            vblock.qc().elect_height());
    if (s != Status::kSuccess) {
        ZJC_ERROR("qc verify failed, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.qc().view(), vblock.qc().view(),
            vblock.qc().network_id(),
            vblock.qc().pool_index(),
            vblock.block_info().height(),
            vblock.qc().elect_height(),
            network::kRootCongressNetworkId,
            vblock.qc().network_id(),
            vblock.qc().elect_height());
        return s;
    }

    ZJC_DEBUG("qc verify success, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.qc().view(), vblock.qc().view(),
            vblock.qc().network_id(),
            vblock.qc().pool_index(),
            vblock.block_info().height(),
            vblock.qc().elect_height(),
            network::kRootCongressNetworkId,
            vblock.qc().network_id(),
            vblock.qc().elect_height());    
#else
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
    ZJC_DEBUG("view block hash: %s, get hash: %s, now check bls sign: x: %s, y: %s, z: %s", 
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
        ZJC_ERROR("qc verify failed, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.qc().view(), vblock.qc().view(),
            vblock.qc().network_id(),
            vblock.qc().pool_index(),
            vblock.block_info().height(),
            vblock.qc().elect_height(),
            network::kRootCongressNetworkId,
            vblock.qc().network_id(),
            vblock.qc().elect_height());
        return s;
    }

    ZJC_DEBUG("qc verify success, s: %d, blockview: %lu, "
            "qcview: %lu, %u_%u_%lu, block elect height: %lu, elect height: %u_%u_%lu",
            s, vblock.qc().view(), vblock.qc().view(),
            vblock.qc().network_id(),
            vblock.qc().pool_index(),
            vblock.block_info().height(),
            vblock.qc().elect_height(),
            network::kRootCongressNetworkId,
            vblock.qc().network_id(),
            vblock.qc().elect_height());
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
        case PRE_RESET_TIMER:
            ADD_DEBUG_PROCESS_TIMESTAMP();
            hotstuff(hotstuff_msg.pool_index())->HandlePreResetTimerMsg(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
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
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; pool_idx++) {
        msg_ptr->times_idx = 0;
        if (common::GlobalInfo::Instance()->pools_with_thread()[pool_idx] == thread_index) {
            bool has_user_tx = false;
            bool has_system_tx = false;
            ADD_DEBUG_PROCESS_TIMESTAMP();
            pacemaker(pool_idx)->HandleTimerMessage(msg_ptr);
            ADD_DEBUG_PROCESS_TIMESTAMP();
            auto tx_valid_func = [&](
                    const address::protobuf::AddressInfo& addr_info, 
                    pools::protobuf::TxMessage& tx_info) -> int {
                auto latest_block = pool_hotstuff_[pool_idx]->view_block_chain()->HighViewBlock();
                if (!latest_block) {
                    return false;
                }
                
                if (pools::IsUserTransaction(tx_info.step())) {
                    return pool_hotstuff_[pool_idx]->view_block_chain()->CheckTxNonceValid(
                        addr_info.addr(), 
                        tx_info.nonce(), 
                        latest_block->qc().view_block_hash());
                }
                
                zjcvm::ZjchainHost zjc_host;
                zjc_host.parent_hash_ = latest_block->qc().view_block_hash();
                zjc_host.view_block_chain_ = pool_hotstuff_[pool_idx]->view_block_chain();
                std::string val;
                if (zjc_host.GetKeyValue(tx_info.to(), tx_info.key(), &val) == zjcvm::kZjcvmSuccess) {
                    return 1;
                }

                ZJC_DEBUG("not user tx unique hash success to: %s, unique hash: %s",
                    common::Encode::HexEncode(tx_info.to()).c_str(),
                    common::Encode::HexEncode(tx_info.key()).c_str());
                return 0;
            };

            if (now_tm_ms >= prev_check_timer_single_tm_ms_[pool_idx] + 1000lu) {
                prev_check_timer_single_tm_ms_[pool_idx] = now_tm_ms;
                has_system_tx = block_wrapper(pool_idx)->HasSingleTx(msg_ptr, tx_valid_func);
                ZJC_DEBUG("pool: %d check hash system tx: %d", pool_idx, has_system_tx);
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
            ZJC_WARN("tps: %.2f", tps);
        }
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}


void HotstuffManager::PopPoolsMessage() {
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    while (!destroy_) {
        auto consensus_tx_count = 0;
        for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
            while (!destroy_) {
                transport::MessagePtr msg_ptr = nullptr;
                if (!consensus_add_tx_msgs_[i].pop(&msg_ptr) || msg_ptr == nullptr) {
                    break;
                }
                
                const google::protobuf::RepeatedPtrField<shardora::pools::protobuf::TxMessage>* txs_ptr = nullptr;
                if (msg_ptr->header.hotstuff().has_pre_reset_timer_msg()) {
                    txs_ptr = &msg_ptr->header.hotstuff().pre_reset_timer_msg().txs();
                } else {
                    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();
                    txs_ptr = &vote_msg.txs();
                }

                auto& txs = *txs_ptr;
                consensus_tx_count += txs.size();
                ZJC_DEBUG("tps success handle message hash64: %lu, tx size: %d", msg_ptr->header.hash64(), txs.size());
                for (uint32_t i = 0; i < uint32_t(txs.size()); i++) {
                    auto* tx = &txs[i];
                    protos::AddressInfoPtr address_info = nullptr;
                    std::string from_id;
                    if (!pools::IsUserTransaction(tx->step())) {
                        continue;
                    }
                    
                    if (tx->pubkey().size() == 64u) {
                        security::GmSsl gmssl;
                        from_id = gmssl.GetAddress(tx->pubkey());
                    } else if (tx->pubkey().size() > 128u) {
                        security::Oqs oqs;
                        from_id = oqs.GetAddress(tx->pubkey());
                    } else {
                        from_id = security_ptr_->GetAddress(tx->pubkey());
                    }

                    uint32_t pool_index = common::kInvalidPoolIndex;
                    if (tx->step() == pools::protobuf::kContractExcute) {
                        pool_index = common::GetAddressPoolIndex(tx->to());
                        address_info = pool_hotstuff_[pool_index]->view_block_chain()->ChainGetAccountInfo(tx->to());
                    } else {
                        pool_index = common::GetAddressPoolIndex(tx->to());
                        address_info = pool_hotstuff_[pool_index]->view_block_chain()->ChainGetAccountInfo(from_id);
                    }
            
                    if (!address_info) {
                        ZJC_WARN("get address failed nonce: %lu", tx->nonce());
                        continue;
                    }
            
                    std::string contract_prepayment_id;
                    pools::TxItemPtr tx_ptr = nullptr;
                    switch (tx->step()) {
                    case pools::protobuf::kNormalFrom:
                        tx_ptr = std::make_shared<consensus::FromTxItem>(
                                msg_ptr, i, account_mgr_, security_ptr_, address_info);
                        // ADD_TX_DEBUG_INFO((const_cast<pools::protobuf::TxMessage*>(tx)));
                        break;
                    case pools::protobuf::kContractCreate:
                        tx_ptr = std::make_shared<consensus::ContractUserCreateCall>(
                                contract_mgr_, 
                                db_, 
                                msg_ptr, i, 
                                account_mgr_, 
                                security_ptr_, 
                                address_info);
                        contract_prepayment_id = tx->to() + from_id;
                        break;
                    case pools::protobuf::kCreateLibrary:
                        tx_ptr = std::make_shared<consensus::CreateLibrary>(
                                msg_ptr, i, 
                                account_mgr_, 
                                security_ptr_, 
                                address_info);
                        contract_prepayment_id = tx->to() + from_id;
                        break;
                    case pools::protobuf::kContractExcute:
                        tx_ptr = std::make_shared<consensus::ContractCall>(
                                contract_mgr_, 
                                gas_prepayment_, 
                                db_, 
                                msg_ptr, i,
                                account_mgr_, 
                                security_ptr_, 
                                address_info);
                        contract_prepayment_id = tx->to() + from_id;
                        break;
                    case pools::protobuf::kContractGasPrepayment:
                        tx_ptr = std::make_shared<consensus::ContractUserCall>(
                                db_, 
                                msg_ptr, i,
                                account_mgr_, 
                                security_ptr_, 
                                address_info);
                        contract_prepayment_id = tx->to() + from_id;
                        break;
                    case pools::protobuf::kJoinElect:
                    {
                        auto keypair = bls::AggBls::Instance()->GetKeyPair();
                        tx_ptr = std::make_shared<consensus::JoinElectTxItem>(
                            msg_ptr, i, 
                            account_mgr_, 
                            security_ptr_, 
                            prefix_db_, 
                            elect_mgr_, 
                            address_info,
                            (*tx).pubkey(),
                            keypair->pk(),
                            keypair->proof());
                        break;
                    }
                    default:
                        break;
                    }
                    
                    if (tx_ptr != nullptr) {
                        auto tx_hash = pools::GetTxMessageHash(*tx);
                        if (tx_ptr->tx_info->pubkey().size() == 64u) {
                            security::GmSsl gmssl;
                            if (gmssl.Verify(
                                    tx_hash,
                                    tx_ptr->tx_info->pubkey(),
                                    tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                                assert(false);
                            } else {
                                pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                            }
                        } else if (tx_ptr->tx_info->pubkey().size() > 128u) {
                            security::Oqs oqs;
                            if (oqs.Verify(
                                    tx_hash,
                                    tx_ptr->tx_info->pubkey(),
                                    tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                                assert(false);
                            } else {
                                pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                            }
                        } else {
                            if (security_ptr_->Verify(
                                    tx_hash,
                                    tx_ptr->tx_info->pubkey(),
                                    tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                                assert(false);
                            } else {
                                pools_mgr_->BackupConsensusAddTxs(msg_ptr, address_info->pool_index(), tx_ptr);
                            }
                        }
                    }
                }
            }
        }
        if (consensus_tx_count > 0)
        ZJC_INFO("tps success add consensus_tx_count: %lu", consensus_tx_count);
        std::unique_lock<std::mutex> lock(pop_tx_mu_);
        pop_tx_con_.wait_for(lock, std::chrono::milliseconds(10));
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
    // pools_mgr_->RegisterCreateTxFunction(
    //     pools::protobuf::kContractCreateByRootTo,
    //     std::bind(&HotstuffManager::CreateContractByRootToTx, this, std::placeholders::_1));
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
