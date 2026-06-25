#include "bls/bls_manager.h"

#include <bls/bls_utils.h>
#include "bls/dkg_cache.h"
#include <dkg/dkg.h>
#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/common/profiling.hpp>

#include "bls/bls_sign.h"
#include "bls/agg_bls.h"
#include "common/global_info.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/get_proto_hash.h"
#include "protos/prefix_db.h"
#include "init/init_utils.h"
#include "transport/processor.h"
#include "common/encode.h"

namespace shardora {

namespace bls {

void initLibSnark() noexcept {
    static bool s_initialized = []() noexcept
    {
        libff::inhibit_profiling_info = true;
        libff::inhibit_profiling_counters = true;
        libff::alt_bn128_pp::init_public_params();
        return true;
    }();
    (void)s_initialized;
}

BlsManager::BlsManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<ck::ClickHouseClient> ck_client) 
        : security_(security), db_(db), ck_client_(ck_client) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    initLibSnark();
    network::Route::Instance()->RegisterMessage(
        common::kBlsMessage,
        std::bind(&BlsManager::HandleMessage, this, std::placeholders::_1));
    bls_tick_.CutOff(10000000lu, std::bind(&BlsManager::TimerMessage, this));
    dkg_cache_ = std::make_shared<DkgCache>(prefix_db_);
}

BlsManager::~BlsManager() {}

void BlsManager::PoolTimerMessage() {
    if (network::DhtManager::Instance()->valid_count(
            common::GlobalInfo::Instance()->network_id()) >=
            common::GlobalInfo::Instance()->sharding_min_nodes_count()) {
        PopFinishMessage();
        BatchVerifyFinishItems();

        auto now_tm_ms = common::TimeUtils::TimestampMs();
        auto etime = common::TimeUtils::TimestampMs();
        if (etime - now_tm_ms >= 10) {
            SHARDORA_WARN("BlsManager handle message end use time: %lu", (etime - now_tm_ms));
        }
    }

    // bls_tick_.CutOff(100000lu, std::bind(&BlsManager::TimerMessage, this));
}

void BlsManager::TimerMessage() {
    auto tmp_bls = LoadWaitingBls();
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    // SHARDORA_WARN("BlsManager handle message begin.");
    if (tmp_bls != nullptr) {
        tmp_bls->TimerMessage();
    }

    bls_tick_.CutOff(100000lu, std::bind(&BlsManager::TimerMessage, this));
}

void BlsManager::OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        uint64_t prev_elect_height,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block) {
    auto iter = finish_networks_map_.find(sharding_id);
    if (iter != finish_networks_map_.end()) {
        finish_networks_map_.erase(iter);
    }

    auto elect_iter = elect_members_.find(sharding_id);
    if (elect_iter != elect_members_.end()) {
        if (elect_iter->second->height >= elect_height) {
            return;
        }
    }

    auto elect_item = std::make_shared<ElectItem>();
    elect_item->height = elect_height;
    auto members = std::make_shared<common::Members>();
    auto& in = elect_block->in();
    for (int32_t i = 0; i < in.size(); ++i) {
        auto id = security_->GetAddressWithPublicKey(in[i].pubkey());
        members->push_back(std::make_shared<common::BftMember>(
            elect_block->shard_network_id(),
            id,
            in[i].pubkey(),
            i,
            in[i].pool_idx_mod_num()));
        SHARDORA_DEBUG("new elect set elect item index: %u, net: %u, pk: %s", i, elect_block->shard_network_id(),
            common::Encode::HexEncode(in[i].pubkey()).c_str());
    }

    elect_item->members = members;
    elect_members_[sharding_id] = elect_item;
    SHARDORA_DEBUG("sharding: %u, success add new bls dkg, elect_height: %lu, member count: %u",
        sharding_id, elect_height, members->size());
    if (!network::IsSameToLocalShard(sharding_id)) {
        return;
    }

    if (elect_height <= latest_elect_height_) {
        return;
    }

    uint32_t local_member_idx = common::kInvalidUint32;
    for (uint32_t i = 0; i < members->size(); ++i) {
        if (((*members)[i])->id == security_->GetAddress()) {
            local_member_idx = i;
            break;
        }
    }

    if (local_member_idx == common::kInvalidUint32) {
        return;
    }

    latest_elect_height_ = elect_height;
    auto waiting_bls = std::make_shared<bls::BlsDkg>();
    dkg_cache_->Init(local_member_idx, *members, elect_block->shard_network_id());
    waiting_bls->Init(
        this,
        security_,
        0,
        0,
        libff::alt_bn128_Fr::zero(),
        libff::alt_bn128_G2::zero(),
        libff::alt_bn128_G2::zero(),
        db_,
        dkg_cache_,
        ck_client_);
//     SHARDORA_WARN("call OnNewElectionBlock success add new bls dkg, elect_height: %lu", elect_height);
    auto tmp_tm_block_info = LoadLatestTimeblockInfo();
    waiting_bls->OnNewElectionBlock(
        elect_height,
        prev_elect_height,
        members,
        tmp_tm_block_info);
    StoreWaitingBls(waiting_bls);
    SHARDORA_DEBUG("success add new bls dkg, elect_height: %lu, prev valid elect height: %lu",
        elect_height, prev_elect_height);
}

int BlsManager::FirewallCheckMessage(transport::MessagePtr& msg_ptr) try {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (bls_msg.has_finish_req()) {
        if (CheckFinishMessageValid(msg_ptr) != transport::kFirewallCheckSuccess) {
            return transport::kFirewallCheckError;
        }
    } else {
        auto waiting_bls = LoadWaitingBls();
        if (waiting_bls != nullptr) {
            if (!waiting_bls->CheckBlsMessageValid(msg_ptr)) {
                BLS_ERROR("check firewall failed!");
                return transport::kFirewallCheckError;
            }
        }
    }

    SHARDORA_DEBUG("check firewall success!");
    return transport::kFirewallCheckSuccess;
} catch (std::exception& e) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    BLS_ERROR("catch error: %s, %s", e.what(), ProtobufToJson(bls_msg).c_str());
    return transport::kFirewallCheckError;
}

int BlsManager::CheckFinishMessageValid(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (bls_msg.finish_req().network_id() < network::kRootCongressNetworkId ||
            bls_msg.finish_req().network_id() >= network::kConsensusShardEndNetworkId) {
        SHARDORA_WARN("finish network error: %d", bls_msg.finish_req().network_id());
        return transport::kFirewallCheckError;
    }

    auto elect_iter = elect_members_.find(bls_msg.finish_req().network_id());
    if (elect_iter == elect_members_.end()) {
        SHARDORA_WARN("finish network error: %d", bls_msg.finish_req().network_id());
        return transport::kFirewallCheckError;
    }

    if (elect_iter->second->height != bls_msg.elect_height()) {
        SHARDORA_WARN("finish network error: %d, elect height now: %lu, req: %lu",
            bls_msg.finish_req().network_id(),
            elect_iter->second->height,
            bls_msg.elect_height());
        return transport::kFirewallCheckError;
    }

    common::MembersPtr members = elect_iter->second->members;
    if (members == nullptr || bls_msg.index() >= members->size()) {
        BLS_ERROR("not get waiting network members network id: %u, index: %d",
            bls_msg.finish_req().network_id(), bls_msg.index());
        return transport::kFirewallCheckError;
    }

    std::string msg_hash;
    protos::GetProtoHash(msg_ptr->header, &msg_hash);
    if (security_->Verify(
            msg_hash,
            (*members)[bls_msg.index()]->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        BLS_ERROR("verify message failed network id: %u, index: %d",
            bls_msg.finish_req().network_id(), bls_msg.index());
        return transport::kFirewallCheckError;
    }

    std::vector<std::string> pkey_str = {
            bls_msg.finish_req().pubkey().x_c0(),
            bls_msg.finish_req().pubkey().x_c1(),
            bls_msg.finish_req().pubkey().y_c0(),
            bls_msg.finish_req().pubkey().y_c1()
    };
    auto t = common::GetSignerCount(members->size());
    BLSPublicKey pkey(std::make_shared<std::vector<std::string>>(pkey_str));
    std::vector<std::string> common_pkey_str = {
            bls_msg.finish_req().common_pubkey().x_c0(),
            bls_msg.finish_req().common_pubkey().x_c1(),
            bls_msg.finish_req().common_pubkey().y_c0(),
            bls_msg.finish_req().common_pubkey().y_c1()
    };
    BLSPublicKey common_pkey(std::make_shared<std::vector<std::string>>(common_pkey_str));
    std::string common_pk_str = bls_msg.finish_req().common_pubkey().x_c0() +
        bls_msg.finish_req().common_pubkey().x_c1() +
        bls_msg.finish_req().common_pubkey().y_c0() +
        bls_msg.finish_req().common_pubkey().y_c1();
    std::string cpk_hash = common::Hash::keccak256(common_pk_str);
    libff::alt_bn128_G1 sign;
    sign.X = libff::alt_bn128_Fq(bls_msg.finish_req().bls_sign_x().c_str());
    sign.Y = libff::alt_bn128_Fq(bls_msg.finish_req().bls_sign_y().c_str());
    sign.Z = libff::alt_bn128_Fq::one();
    std::string verify_hash;
    libff::alt_bn128_G1 g1_hash;
    GetLibffHash(cpk_hash, &g1_hash);
    if (Verify(
            t,
            members->size(),
            *pkey.getPublicKey(),
            sign,
            g1_hash,
            &verify_hash) != bls::kBlsSuccess) {
        SHARDORA_WARN("verify bls finish bls sign error t: %d, size: %d, cpk_hash: %s, pk: %s",
            t, members->size(), common::Encode::HexEncode(cpk_hash).c_str(), common_pk_str.c_str());
        return transport::kFirewallCheckError;
    }

    return transport::kFirewallCheckSuccess;
}

void BlsManager::OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    auto tmp_latest_tm = LoadLatestTimeblockInfo();
    if (tmp_latest_tm != nullptr) {
        if (latest_time_block_height <= tmp_latest_tm->latest_time_block_height) {
            return;
        }
    }

    auto timeblock_info = std::make_shared<TimeBlockItem>();
    timeblock_info->lastest_time_block_tm = lastest_time_block_tm / 1000lu;
    timeblock_info->latest_time_block_height = latest_time_block_height;
    timeblock_info->vss_random = vss_random;
    StoreLatestTimeblockInfo(timeblock_info);
}

void BlsManager::SetUsedElectionBlock(
        uint64_t elect_height,
        uint32_t network_id,
        uint32_t member_count,
        const libff::alt_bn128_G2& common_public_key) try {
    if (max_height_ != common::kInvalidUint64 && elect_height <= max_height_) {
        BLS_ERROR("elect_height error: %lu, %lu", elect_height, max_height_);
        return;
    }

    max_height_ = elect_height;
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

int BlsManager::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const libff::alt_bn128_G1& g1_hash,
        libff::alt_bn128_G1* bn_sign) {    
    BlsSign::Sign(t, n, local_sec_key, g1_hash, bn_sign);
    // bn_sign->to_affine_coordinates();
    // std::string sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign->X);
    // std::string sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign->Y);
    // std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(local_sec_key);
    // BLSPublicKeyShare pkey(local_sec_key, t, n);
    // std::shared_ptr< std::vector< std::string > > strs = pkey.toString();
    // SHARDORA_WARN("sign t: %u, n: %u, , pk: %s,%s,%s,%s, sign x: %s, sign y: %s, sign msg: %s,%s,%s",
    //     t, n,
    //     (*strs)[0].c_str(), (*strs)[1].c_str(), (*strs)[2].c_str(), (*strs)[3].c_str(),
    //     (sign_x).c_str(), (sign_y).c_str(),
    //     libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
    //     libBLS::ThresholdUtils::fieldElementToString(g1_hash.Y).c_str(),
    //     libBLS::ThresholdUtils::fieldElementToString(g1_hash.Z).c_str());
    // std::string verify_hash;
    // //assert(Verify(t, n, *pkey.getPublicKey(), *bn_sign, g1_hash, &verify_hash) == kBlsSuccess);
    return kBlsSuccess;
}

int BlsManager::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const libff::alt_bn128_G1& g1_hash,
        std::string* sign_x,
        std::string* sign_y) try {
//     std::lock_guard<std::mutex> guard(sign_mutex_);
    libff::alt_bn128_G1 bn_sign;
    BlsSign::Sign(t, n, local_sec_key, g1_hash, &bn_sign);
    bn_sign.to_affine_coordinates();
    *sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign.X);
    *sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign.Y);
// #ifndef NDEBUG
//     std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(local_sec_key);
//     BLSPublicKeyShare pkey(local_sec_key, t, n);
//     std::shared_ptr<std::vector<std::string>> strs = pkey.toString();
//     SHARDORA_WARN("sign t: %u, , n: %u, , pk: %s,%s,%s,%s sign x: %s, sign y: %s, sign msg: %s,%s,%s",
//         t, n, 
//         (*strs)[0].c_str(), (*strs)[1].c_str(), (*strs)[2].c_str(), (*strs)[3].c_str(),
//         (*sign_x).c_str(), (*sign_y).c_str(),
//         libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
//         libBLS::ThresholdUtils::fieldElementToString(g1_hash.Y).c_str(),
//         libBLS::ThresholdUtils::fieldElementToString(g1_hash.Z).c_str());
//     std::string verify_hash;
//     //assert(Verify(t, n, *pkey.getPublicKey(), bn_sign, g1_hash, &verify_hash) == kBlsSuccess);
// #endif
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

int BlsManager::Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& pubkey,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        std::string* verify_hash) try {
    if (pubkey == libff::alt_bn128_G2::zero()) {
        auto sign_ptr = const_cast<libff::alt_bn128_G1*>(&sign);
        sign_ptr->to_affine_coordinates();
        auto sign_x = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->X);
        auto sign_y = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->Y);
        BLS_ERROR("public key error: zero,msg sign x: %s, sign y: %s",
            sign_x.c_str(),
            sign_y.c_str());
        return kBlsError;
    }

// #ifndef NDEBUG
//     auto bn_sign = sign;
//     bn_sign.to_affine_coordinates();
//     auto pk_str = libBLS::ThresholdUtils::fieldElementToString(pubkey.X.c0);
//     auto sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign.X);
//     auto sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign.Y);
//     SHARDORA_WARN("verify t: %u, n: %u, sign x: %s, sign y: %s, sign msg: %s,%s,%s, pk: %s",
//         t, n,
//         (sign_x).c_str(), (sign_y).c_str(),
//         libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
//         libBLS::ThresholdUtils::fieldElementToString(g1_hash.Y).c_str(),
//         libBLS::ThresholdUtils::fieldElementToString(g1_hash.Z).c_str(),
//         pk_str.c_str());
// #endif
    return BlsSign::Verify(t, n, sign, g1_hash, pubkey, verify_hash);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

int BlsManager::VerifyFast(
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey) try {
    if (pkey == libff::alt_bn128_G2::zero()) {
        return kBlsError;
    }

    return BlsSign::VerifyFast(sign, g1_hash, pkey);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

int BlsManager::GetVerifyHash(
            uint32_t t,
            uint32_t n,
            const libff::alt_bn128_G1& g1_hash,
            const libff::alt_bn128_G2& pkey,
            std::string* verify_hash) try {
    if (pkey == libff::alt_bn128_G2::zero()) {
        return kBlsError;
    }
    return BlsSign::GetVerifyHash(t, n, g1_hash, pkey, verify_hash);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

int BlsManager::GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        std::string* verify_hash) try {
    if (sign == libff::alt_bn128_G1::zero()) {
        return kBlsError;
    }

    return BlsSign::GetVerifyHash(t, n, sign, verify_hash);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

void BlsManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (bls_msg.has_finish_req()) {
        finish_msg_queue_.push(msg_ptr);
        SHARDORA_DEBUG("queue size finish_msg_queue_: %d, hash64: %lu",
            finish_msg_queue_.size(), msg_ptr->header.hash64());
        return;
    }

    // Handle finish sync request
    if (bls_msg.has_finish_sync_req()) {
        HandleFinishSyncRequest(msg_ptr);
        return;
    }

    auto waiting_bls = LoadWaitingBls();
    if (waiting_bls != nullptr) {
        waiting_bls->HandleMessage(msg_ptr);
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

int BlsManager::GetLibffHash(const std::string& str_hash, libff::alt_bn128_G1* g1_hash) {
    return BlsSign::GetLibffHash(str_hash, g1_hash);
}

void BlsManager::PopFinishMessage() {
    while (true) {
        transport::MessagePtr msg_ptr = nullptr;
        if (!finish_msg_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
            break;
        }

        HandleFinish(msg_ptr);
    }
}

void BlsManager::HandleFinish(const transport::MessagePtr& msg_ptr) {
    SHARDORA_DEBUG("0 handle finish called hash64: %lu", msg_ptr->header.hash64());
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (bls_msg.finish_req().network_id() < network::kRootCongressNetworkId ||
            bls_msg.finish_req().network_id() >= network::kConsensusShardEndNetworkId) {
        SHARDORA_WARN("finish network error: %d", bls_msg.finish_req().network_id());
        return;
    }

    auto elect_iter = elect_members_.find(bls_msg.finish_req().network_id());
    if (elect_iter == elect_members_.end()) {
        SHARDORA_WARN("finish network error: %d", bls_msg.finish_req().network_id());
        return;
    }

    if (elect_iter->second->height != bls_msg.elect_height()) {
        SHARDORA_WARN("finish network error: %d, elect height now: %lu, req: %lu",
            bls_msg.finish_req().network_id(),
            elect_iter->second->height,
            bls_msg.elect_height());
        return;
    }

    common::MembersPtr members = elect_iter->second->members;
    if (members == nullptr || bls_msg.index() >= members->size()) {
        BLS_ERROR("not get waiting network members network id: %u, index: %d",
            bls_msg.finish_req().network_id(), bls_msg.index());
        return;
    }

    std::string msg_hash;
    protos::GetProtoHash(msg_ptr->header, &msg_hash);
    if (security_->Verify(
            msg_hash,
            (*members)[bls_msg.index()]->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        BLS_ERROR("verify message failed network id: %u, index: %d",
            bls_msg.finish_req().network_id(), bls_msg.index());
        return;
    }

    std::vector<std::string> pkey_str = {
            bls_msg.finish_req().pubkey().x_c0(),
            bls_msg.finish_req().pubkey().x_c1(),
            bls_msg.finish_req().pubkey().y_c0(),
            bls_msg.finish_req().pubkey().y_c1()
    };
    auto t = common::GetSignerCount(members->size());
    BLSPublicKey pkey(std::make_shared<std::vector<std::string>>(pkey_str));
    std::vector<std::string> common_pkey_str = {
            bls_msg.finish_req().common_pubkey().x_c0(),
            bls_msg.finish_req().common_pubkey().x_c1(),
            bls_msg.finish_req().common_pubkey().y_c0(),
            bls_msg.finish_req().common_pubkey().y_c1()
    };
    BLSPublicKey common_pkey(std::make_shared<std::vector<std::string>>(common_pkey_str));
    std::string common_pk_str = bls_msg.finish_req().common_pubkey().x_c0() +
        bls_msg.finish_req().common_pubkey().x_c1() +
        bls_msg.finish_req().common_pubkey().y_c0() +
        bls_msg.finish_req().common_pubkey().y_c1();
    std::string cpk_hash = common::Hash::keccak256(common_pk_str);
    libff::alt_bn128_G1 sign;
    sign.X = libff::alt_bn128_Fq(bls_msg.finish_req().bls_sign_x().c_str());
    sign.Y = libff::alt_bn128_Fq(bls_msg.finish_req().bls_sign_y().c_str());
    sign.Z = libff::alt_bn128_Fq::one();
    // std::string verify_hash;
    // libff::alt_bn128_G1 g1_hash;
    // GetLibffHash(cpk_hash, &g1_hash);
    // if (Verify(
    //         t,
    //         members->size(),
    //         *pkey.getPublicKey(),
    //         sign,
    //         g1_hash,
    //         &verify_hash) != bls::kBlsSuccess) {
    //     SHARDORA_WARN("verify bls finish bls sign error t: %d, size: %d, cpk_hash: %s, pk: %s",
    //         t, members->size(), common::Encode::HexEncode(cpk_hash).c_str(), common_pk_str.c_str());
    //     return;
    // }

    BlsFinishItemPtr finish_item = nullptr;
    auto iter = finish_networks_map_.find(bls_msg.finish_req().network_id());
    if (iter == finish_networks_map_.end()) {
        finish_item = std::make_shared<BlsFinishItem>();
        finish_networks_map_[bls_msg.finish_req().network_id()] = finish_item;
    } else {
        finish_item = iter->second;
    }

    if (finish_item->verified[bls_msg.index()]) {
        SHARDORA_DEBUG("1 handle finish called hash64: %lu", msg_ptr->header.hash64());
        return;
    }

    finish_item->verified[bls_msg.index()] = true;
    auto common_pk_iter = finish_item->common_pk_map.find(cpk_hash);
    if (common_pk_iter == finish_item->common_pk_map.end()) {
        finish_item->common_pk_map[cpk_hash] = *common_pkey.getPublicKey();
    }

    finish_item->all_public_keys[bls_msg.index()] = *pkey.getPublicKey();
    finish_item->all_bls_signs[bls_msg.index()] = sign;
    finish_item->all_common_public_keys[bls_msg.index()] = *common_pkey.getPublicKey();
    auto cpk_iter = finish_item->max_public_pk_map.find(cpk_hash);
    if (cpk_iter == finish_item->max_public_pk_map.end()) {
        finish_item->max_public_pk_map[cpk_hash] = 1;
    } else {
        ++cpk_iter->second;
    }
    
    // Always add to pending queue when count reaches t, then continue adding for batch verification
    if (finish_item->max_public_pk_map[cpk_hash] >= t) {
        finish_item->pending_verify_indices.push_back(bls_msg.index());
    }

    if (finish_item->success_verified) {
        SHARDORA_DEBUG("success check all members agg signature, elect_height: %lu",
            bls_msg.elect_height());
    }

    SHARDORA_DEBUG("handle finish success. sharding: %u, member index: %u, cpk_hash: %s, common pk: %s",
        bls_msg.finish_req().network_id(),
        bls_msg.index(),
        common::Encode::HexEncode(cpk_hash).c_str(),
        common_pkey_str[0].c_str());
    auto max_iter = finish_item->max_bls_members.find(cpk_hash);
    if (max_iter != finish_item->max_bls_members.end()) {
        ++max_iter->second->count;
        SHARDORA_DEBUG("handle finish success count: %d sharding: %u, member index: %u, cpk_hash: %s.",
            max_iter->second->count,
            bls_msg.finish_req().network_id(),
            bls_msg.index(),
            common::Encode::HexEncode(cpk_hash).c_str());
        if (max_iter->second->count > finish_item->max_finish_count) {
            finish_item->max_finish_count = max_iter->second->count;
            finish_item->max_finish_hash = cpk_hash;
        }

        return;
    }

    std::vector<uint64_t> bitmap_data;
    for (int32_t i = 0; i < bls_msg.finish_req().bitmap_size(); ++i) {
        bitmap_data.push_back(bls_msg.finish_req().bitmap(i));
    }

    common::Bitmap bitmap(bitmap_data);
    auto item = std::make_shared<MaxBlsMemberItem>(1, bitmap);
    finish_item->max_bls_members[cpk_hash] = item;
    if (finish_item->max_finish_count == 0) {
        finish_item->max_finish_count = 1;
        finish_item->max_finish_hash = cpk_hash;
    }
}

void BlsManager::HandleFinishSyncRequest(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    
    if (!bls_msg.has_finish_sync_req()) {
        BLS_WARN("[HandleSyncReq] no finish_sync_req in message");
        return;
    }

    auto& sync_req = bls_msg.finish_sync_req();
    uint32_t network_id = sync_req.network_id();
    
    BLS_INFO("[HandleSyncReq] network %u: received sync request for %d missing nodes from member %u",
             network_id, sync_req.missing_indices_size(), bls_msg.index());

    // Check if we have finish item for this network
    auto finish_iter = finish_networks_map_.find(network_id);
    if (finish_iter == finish_networks_map_.end()) {
        BLS_DEBUG("[HandleSyncReq] network %u: finish_networks_map_ not found", network_id);
        return;
    }

    BlsFinishItemPtr finish_item = finish_iter->second;

    // Check if we have elect members for this network
    auto elect_iter = elect_members_.find(network_id);
    if (elect_iter == elect_members_.end()) {
        BLS_DEBUG("[HandleSyncReq] network %u: elect_members_ not found", network_id);
        return;
    }

    auto members = elect_iter->second->members;
    if (!members) {
        BLS_DEBUG("[HandleSyncReq] network %u: members is null", network_id);
        return;
    }

    uint32_t n = static_cast<uint32_t>(members->size());

    // Check if we are in the finish period
    auto waiting_bls = LoadWaitingBls();
    if (!waiting_bls) {
        BLS_DEBUG("[HandleSyncReq] network %u: waiting_bls_ is null", network_id);
        return;
    }

    if (!waiting_bls->IsFinishPeriod()) {
        BLS_DEBUG("[HandleSyncReq] network %u: not in finish period", network_id);
        return;
    }

    // Get max finish hash
    if (finish_item->max_finish_hash.empty()) {
        BLS_DEBUG("[HandleSyncReq] network %u: max_finish_hash is empty", network_id);
        return;
    }

    // Find the bitmap for max finish hash
    auto max_bls_iter = finish_item->max_bls_members.find(finish_item->max_finish_hash);
    if (max_bls_iter == finish_item->max_bls_members.end()) {
        BLS_DEBUG("[HandleSyncReq] network %u: max_bls_members not found", network_id);
        return;
    }

    // Get requester's member index from message
    uint32_t requester_idx = bls_msg.index();
    if (requester_idx >= n) {
        BLS_WARN("[HandleSyncReq] network %u: invalid requester_idx %u >= %u",
                 network_id, requester_idx, n);
        return;
    }

    // Get requester's address
    std::string requester_id = (*members)[requester_idx]->id;

    // Send finish messages for the requested missing nodes
    uint32_t sent_count = 0;
    for (int32_t i = 0; i < sync_req.missing_indices_size(); ++i) {
        uint32_t missing_idx = sync_req.missing_indices(i);
        
        // Validate index
        if (missing_idx >= n) {
            BLS_WARN("[HandleSyncReq] network %u: invalid missing_idx %u >= %u",
                     network_id, missing_idx, n);
            continue;
        }

        // Check if we have this node's finish message
        if (!finish_item->verified[missing_idx]) {
            BLS_DEBUG("[HandleSyncReq] network %u: we don't have finish message for node %u",
                      network_id, missing_idx);
            continue;
        }

        // Create finish message for this missing node
        auto response_msg = std::make_shared<transport::TransportMessage>();
        auto& resp_header = response_msg->header;
        resp_header.set_src_sharding_id(network_id);
        resp_header.set_type(common::kBlsMessage);
        resp_header.set_hop_count(0);
        resp_header.set_des_dht_key(requester_id);
        
        auto& resp_bls_msg = *resp_header.mutable_bls_proto();
        resp_bls_msg.set_index(missing_idx);
        resp_bls_msg.set_elect_height(elect_iter->second->height);
        
        auto finish_msg = resp_bls_msg.mutable_finish_req();
        finish_msg->set_network_id(network_id);
        
        // Add bitmap
        auto& bitmap_data = max_bls_iter->second->bitmap.data();
        for (uint32_t j = 0; j < bitmap_data.size(); ++j) {
            finish_msg->add_bitmap(bitmap_data[j]);
        }

        // Add public key
        auto& pk = finish_item->all_public_keys[missing_idx];
        auto pk_msg = finish_msg->mutable_pubkey();
        pk.to_affine_coordinates();
        pk_msg->set_x_c0(libBLS::ThresholdUtils::fieldElementToString(pk.X.c0));
        pk_msg->set_x_c1(libBLS::ThresholdUtils::fieldElementToString(pk.X.c1));
        pk_msg->set_y_c0(libBLS::ThresholdUtils::fieldElementToString(pk.Y.c0));
        pk_msg->set_y_c1(libBLS::ThresholdUtils::fieldElementToString(pk.Y.c1));

        // Add common public key
        auto& common_pk = finish_item->all_common_public_keys[missing_idx];
        auto common_pk_msg = finish_msg->mutable_common_pubkey();
        common_pk.to_affine_coordinates();
        common_pk_msg->set_x_c0(libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0));
        common_pk_msg->set_x_c1(libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c1));
        common_pk_msg->set_y_c0(libBLS::ThresholdUtils::fieldElementToString(common_pk.Y.c0));
        common_pk_msg->set_y_c1(libBLS::ThresholdUtils::fieldElementToString(common_pk.Y.c1));

        // Add BLS signature
        auto& bls_sign = finish_item->all_bls_signs[missing_idx];
        bls_sign.to_affine_coordinates();
        finish_msg->set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_sign.X));
        finish_msg->set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_sign.Y));

        BLS_INFO("[HandleSyncReq] network %u: sending finish message for node %u to requester",
                 network_id, missing_idx);
        
        network::Route::Instance()->Send(response_msg);
        ++sent_count;
    }

    BLS_INFO("[HandleSyncReq] network %u: sent %u finish messages to requester (requested %d)",
             network_id, sent_count, sync_req.missing_indices_size());
}

static const uint64_t kBatchVerifyIntervalMs = 30000u;  // 30 seconds
static const uint64_t kBatchVerifyFastIntervalMs = 3000u;  // 3 seconds

void BlsManager::BatchVerifyFinishItems() {
    uint64_t now_ms = common::TimeUtils::TimestampMs();
    uint64_t now_us = common::TimeUtils::TimestampUs();

    for (auto& [network_id, finish_item] : finish_networks_map_) {
        if (finish_item->success_verified) continue;

        auto elect_iter = elect_members_.find(network_id);
        if (elect_iter == elect_members_.end()) continue;
        auto& members = elect_iter->second->members;
        if (!members) continue;

        uint32_t n = static_cast<uint32_t>(members->size());
        uint32_t t = common::GetSignerCount(n);

        // Count how many nodes have completed their finish message
        uint32_t verified_count = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (finish_item->verified[i]) {
                ++verified_count;
            }
        }

        // If we haven't received enough finish messages yet, and pending list is empty, skip
        if (finish_item->pending_verify_indices.empty() && verified_count < t) {
            continue;
        }

        // Determine verification interval based on DKG elapsed time
        uint64_t verify_interval_ms = kBatchVerifyIntervalMs;
        auto tmp_bls = LoadWaitingBls();
        if (tmp_bls != nullptr && tmp_bls->elect_hegiht() > 0) {
            // Speed up verification to 3 seconds when DKG passes the 9-period mark
            if (now_us > (tmp_bls->begin_time_us() + tmp_bls->dkg_period_us() * 9)) {
                verify_interval_ms = kBatchVerifyFastIntervalMs;
            }
        }

        if (now_ms - finish_item->last_verify_time_ms < verify_interval_ms) continue;

        finish_item->last_verify_time_ms = now_ms;

        if (finish_item->max_finish_hash.empty()) continue;
        auto cpk_map_iter = finish_item->common_pk_map.find(finish_item->max_finish_hash);
        if (cpk_map_iter == finish_item->common_pk_map.end()) continue;
        libff::alt_bn128_G2 common_pk = cpk_map_iter->second;
        common_pk.to_affine_coordinates();

        // Collect all pending (unverified) members with matching cpk.
        std::vector<uint32_t> candidates;
        for (uint32_t idx : finish_item->pending_verify_indices) {
            if (idx < n && finish_item->all_common_public_keys[idx] == common_pk) {
                candidates.push_back(idx);
            }
        }
        finish_item->pending_verify_indices.clear();

        // If not enough, supplement from already verified members.
        if (candidates.size() < t) {
            for (size_t vi = 0; vi < finish_item->verified_valid_index.size(); ++vi) {
                if (finish_item->verified_valid_index[vi] == 0) continue;
                uint32_t idx = static_cast<uint32_t>(finish_item->verified_valid_index[vi] - 1);
                bool dup = false;
                for (uint32_t c : candidates) { if (c == idx) { dup = true; break; } }
                if (!dup) candidates.push_back(idx);
                if (candidates.size() >= t) break;
            }
        }

        // If still not enough, try to collect from all verified members
        if (candidates.size() < t) {
            for (uint32_t i = 0; i < n; ++i) {
                if (!finish_item->verified[i]) continue;
                if (finish_item->all_common_public_keys[i] != common_pk) continue;
                bool dup = false;
                for (uint32_t c : candidates) { if (c == i) { dup = true; break; } }
                if (!dup) candidates.push_back(i);
                if (candidates.size() >= t) break;
            }
        }

        if (candidates.size() < t) {
            SHARDORA_DEBUG("[BatchVerify] net %u: only %zu candidates, need %u, verified_count=%u, skip",
                       network_id, candidates.size(), t, verified_count);
            continue;
        }

        SHARDORA_DEBUG("[BatchVerify] net %u: start verify with %zu candidates (t=%u, verified_count=%u)",
                  network_id, candidates.size(), t, verified_count);

        for (uint32_t idx : candidates) {
            if (finish_item->success_verified) break;
            CheckAggSignValid(t, n, common_pk, finish_item, idx);
        }

        SHARDORA_DEBUG("[BatchVerify] net %u: batch verify done, success=%d",
                  network_id, finish_item->success_verified);
    }
}

void BlsManager::CheckAggSignValid(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        uint32_t member_idx) {
    SHARDORA_DEBUG("now check agg sign valid t: %u, n: %u, pk: %s, mem_idx: %u, finished: %d", 
        t, n,
        common::Encode::HexEncode(
            libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0)).c_str(),
        member_idx, finish_item->success_verified);
    if (finish_item->success_verified) {
        std::vector<libff::alt_bn128_G1> tmp_all_signs = finish_item->verified_valid_signs;
        std::vector<size_t> tmp_idx_vec = finish_item->verified_valid_index;
        tmp_all_signs[member_idx] = finish_item->all_bls_signs[member_idx];
        tmp_idx_vec[member_idx] = member_idx + 1;
        uint32_t tmp_idx = member_idx + 1;
        while (true) {
            if (tmp_idx >= n) {
                tmp_idx = 0;
            }

            if (tmp_idx_vec[tmp_idx] != 0) {
                tmp_idx_vec[tmp_idx] = 0;
                tmp_all_signs[tmp_idx] = libff::alt_bn128_G1::zero();
                break;
            }

            ++tmp_idx;
        }

        if (!VerifyAggSignValid(
                t,
                n,
                common_pk,
                finish_item,
                tmp_all_signs,
                tmp_idx_vec)) {
            finish_item->all_common_public_keys[member_idx] = libff::alt_bn128_G2::zero();
            BLS_ERROR("invalid bls item index: %d", member_idx);
        } else {
            SHARDORA_DEBUG("valid bls item index: %d", member_idx);
        }

        return;
    }

    // choose finished item t to verify
    std::vector<libff::alt_bn128_G1> all_signs(n, libff::alt_bn128_G1::zero());
    std::vector<size_t> idx_vec(n, 0);
    uint32_t start_pos = rand() % n;
    uint32_t i = start_pos;
    uint32_t valid_count = 0;
    uint32_t min_pos = common::kInvalidUint32;
    while (true) {
        if (i >= n) {
            i = 0;
            if (i == start_pos) {
                break;
            }
        }

        if (finish_item->all_common_public_keys[i] == libff::alt_bn128_G2::zero()) {
            ++i;
            continue;
        }

        if (finish_item->all_common_public_keys[i] != common_pk) {
            finish_item->all_common_public_keys[i].to_affine_coordinates();
            ++i;
            continue;
        }

        all_signs[i] = finish_item->all_bls_signs[i];
        idx_vec[i] = i + 1;
        ++valid_count;
        SHARDORA_DEBUG("select member index: %u for verify, valid_count: %u, sign: %s", i, valid_count, 
            libBLS::ThresholdUtils::fieldElementToString(all_signs[i].X).c_str());
        if (valid_count >= t) {
            break;
        }

        ++i;
        if (i == start_pos) {
            break;
        }
    }

    if (valid_count < t) {
        BLS_ERROR("valid_count: %d < t: %d", valid_count, t);
        return;
    }

    if (CheckAndVerifyAll(
            t,
            n,
            common_pk,
            finish_item,
            all_signs,
            idx_vec)) {
        return;
    }

    for (uint32_t i = 0; i < n; ++i) {
        if (finish_item->all_bls_signs[i] == libff::alt_bn128_G1::zero() ||
                finish_item->all_public_keys[i] == libff::alt_bn128_G2::zero() ||
                idx_vec[i] != 0) {
            continue;
        }

        uint32_t rand_weed_pos = rand() % n;
        uint32_t idx = rand_weed_pos + 1;
        while (true) {
            if (idx >= n) {
                idx = 0;
            }

            if (idx_vec[idx] != 0) {
                idx_vec[idx] = 0;
                all_signs[idx] = libff::alt_bn128_G1::zero();
                break;
            }

            if (idx == rand_weed_pos) {
                //assert(false);
                break;
            }

            ++idx;
        }

        idx_vec[i] = i + 1;
        all_signs[i] = finish_item->all_bls_signs[i];
        SHARDORA_DEBUG("weed member index: %u, bls sign: %s",
            i, libBLS::ThresholdUtils::fieldElementToString(all_signs[i].X).c_str());
        if (CheckAndVerifyAll(
                t,
                n,
                common_pk,
                finish_item,
                all_signs,
                idx_vec)) {
            break;
        }
    }
}

bool BlsManager::CheckAndVerifyAll(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        std::vector<libff::alt_bn128_G1>& all_signs,
        std::vector<size_t>& idx_vec) {
    SHARDORA_DEBUG("now check agg sign valid t: %u, n: %u, pk: %s, finished: %d", 
        t, n,
        common::Encode::HexEncode(
            libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0)).c_str(),
        finish_item->success_verified);
    if (VerifyAggSignValid(
            t,
            n,
            common_pk,
            finish_item,
            all_signs,
            idx_vec)) {
        finish_item->success_verified = true;
        finish_item->verified_valid_signs = all_signs;
        finish_item->verified_valid_index = idx_vec;
        // verify all left members
        for (uint32_t member_idx = 0; member_idx < n; ++member_idx) {
            if (finish_item->all_bls_signs[member_idx] == libff::alt_bn128_G1::zero() ||
                    finish_item->all_public_keys[member_idx] == libff::alt_bn128_G2::zero() ||
                    finish_item->verified_valid_index[member_idx] != 0) {
                continue;
            }

            std::vector<libff::alt_bn128_G1> tmp_all_signs = finish_item->verified_valid_signs;
            std::vector<size_t> tmp_idx_vec = finish_item->verified_valid_index;
            tmp_all_signs[member_idx] = finish_item->all_bls_signs[member_idx];
            tmp_idx_vec[member_idx] = member_idx + 1;
            uint32_t tmp_idx = member_idx + 1;
            while (true) {
                if (tmp_idx >= n) {
                    tmp_idx = 0;
                }

                if (tmp_idx_vec[tmp_idx] != 0) {
                    //assert(tmp_idx != member_idx);
                    tmp_idx_vec[tmp_idx] = 0;
                    tmp_all_signs[tmp_idx] = libff::alt_bn128_G1::zero();
                    break;
                }

                ++tmp_idx;
            }

            if (!VerifyAggSignValid(
                    t,
                    n,
                    common_pk,
                    finish_item,
                    tmp_all_signs,
                    tmp_idx_vec)) {
                finish_item->all_common_public_keys[member_idx] = libff::alt_bn128_G2::zero();
                finish_item->all_public_keys[member_idx] == libff::alt_bn128_G2::zero();
            }
        }

        SHARDORA_DEBUG("success check agg sign valid t: %u, n: %u, pk: %s, finished: %d", 
            t, n,
            common::Encode::HexEncode(
                libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0)).c_str(),
            finish_item->success_verified);
        return true;
    }

    SHARDORA_DEBUG("failed check agg sign valid t: %u, n: %u, pk: %s, finished: %d", 
        t, n,
        common::Encode::HexEncode(
            libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0)).c_str(),
        finish_item->success_verified);
    return false;
}

bool BlsManager::VerifyAggSignValid(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        std::vector<libff::alt_bn128_G1>& in_all_signs,
        std::vector<size_t>& in_idx_vec) {
    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec;
    std::string debug_idx;
    for (uint32_t i = 0; i < in_idx_vec.size(); ++i) {
        if (in_idx_vec[i] != 0) {
            idx_vec.push_back(in_idx_vec[i]);
            all_signs.push_back(in_all_signs[i]);
            auto bn_sign = in_all_signs[i];
            bn_sign.to_affine_coordinates();
            auto sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign.X);
            auto sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign.Y);
            debug_idx += std::to_string(in_idx_vec[i]) + "," + sign_x + "," + sign_y + " ";
        }
    }

    try {
#ifdef MOCK_SIGN
        auto bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one()); 
#else
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
        libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
        auto bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
#endif
        libff::alt_bn128_G1 g1_hash;
        GetLibffHash(finish_item->max_finish_hash, &g1_hash);
        auto bn_sign = *bls_agg_sign;
        bn_sign.to_affine_coordinates();
        auto sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign.X);
        auto sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign.Y);

        if (VerifyFast(*bls_agg_sign, g1_hash, common_pk) != bls::kBlsSuccess) {
            SHARDORA_ERROR("verify agg sign failed t: %d, n: %d, hash: %s, g1 hash: %s, agg sign: %s, %s, %s!",
                t, n,
                common::Encode::HexEncode(finish_item->max_finish_hash).c_str(),
                libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
                sign_x.c_str(), sign_y.c_str(), debug_idx.c_str());
            return false;
        }

        SHARDORA_DEBUG("verify agg sign success t: %d, n: %d, hash: %s, g1 hash: %s, agg sign: %s, %s, %s!",
            t, n,
            common::Encode::HexEncode(finish_item->max_finish_hash).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
            sign_x.c_str(), sign_y.c_str(), debug_idx.c_str());
        return true;
    } catch (...) {
        SHARDORA_ERROR("verify agg sign failed");
    }

    return false;
}

int BlsManager::CheckBlsConsensusInfo(const elect::protobuf::ElectBlock& ec_block) {
    return kBlsSuccess;
    // Verify that the Leader's BLS consensus info matches local verified data
    // Requirement: all finish nodes in leader must be in locally verified nodes, 
    // and the count must exceed 80% of total members
    
    uint32_t network_id = ec_block.shard_network_id();
    // Get local verification state
    auto iter = finish_networks_map_.find(network_id);
    if (iter == finish_networks_map_.end()) {
        BLS_WARN("[CheckBLS] net %u: finish_networks_map_ not found", network_id);
        return ec_block.prev_members().bls_pubkey_size() == 0 ? kBlsSuccess : kBlsError;
    }
    
    BlsFinishItemPtr finish_item = iter->second;
    if (!finish_item->success_verified) {
        BLS_WARN("[CheckBLS] net %u: local not success_verified yet", network_id);
        return ec_block.prev_members().bls_pubkey_size() == 0 ? kBlsSuccess : kBlsError;
    }
    
    auto elect_iter = elect_members_.find(network_id);
    if (elect_iter == elect_members_.end()) {
        BLS_WARN("[CheckBLS] net %u: elect_members_ not found", network_id);
        return ec_block.prev_members().bls_pubkey_size() == 0 ? kBlsSuccess : kBlsError;
    }
    
    auto members = elect_iter->second->members;
    if (!members) {
        BLS_WARN("[CheckBLS] net %u: members is null", network_id);
        return ec_block.prev_members().bls_pubkey_size() == 0 ? kBlsSuccess : kBlsError;
    }
    
    auto exchange_member_count = (uint32_t)((float)members->size() * kBlsMaxExchangeMembersRatio);
    if (exchange_member_count < members->size()) {
        ++exchange_member_count;
    }

    auto t = common::GetSignerCount(members->size());
    if (finish_item->max_finish_count < exchange_member_count) {
        BLS_INFO("network: %u, finish_item->max_finish_count < t[%u][%u]",
            ec_block.shard_network_id(),
            finish_item->max_finish_count, exchange_member_count);
        return ec_block.prev_members().bls_pubkey_size() == 0 ? kBlsSuccess : kBlsError;
    }
    
    uint32_t n = static_cast<uint32_t>(members->size());
    if (static_cast<uint32_t>(ec_block.prev_members().bls_pubkey_size()) != n) {
        BLS_WARN("[CheckBLS] net %u: leader member count %d != local %u", 
                 network_id, ec_block.prev_members().bls_pubkey_size(), n);
        return kBlsError;
    }
    
    // Check common public key matches
    if (!ec_block.prev_members().has_common_pubkey()) {
        BLS_WARN("[CheckBLS] net %u: leader has no common_pubkey", network_id);
        return kBlsError;
    }
    
    const auto& leader_common_pk = ec_block.prev_members().common_pubkey();
    std::string leader_common_pk_str = leader_common_pk.x_c0() + leader_common_pk.x_c1() + 
                                       leader_common_pk.y_c0() + leader_common_pk.y_c1();
    std::string leader_cpk_hash = common::Hash::keccak256(leader_common_pk_str);
    if (leader_cpk_hash != finish_item->max_finish_hash) {
        BLS_WARN("[CheckBLS] net %u: leader cpk_hash %s != local %s", 
                 network_id,
                 common::Encode::HexEncode(leader_cpk_hash).c_str(),
                 common::Encode::HexEncode(finish_item->max_finish_hash).c_str());
        return kBlsError;
    }
    
    // Verify each member's BLS public key
    // Requirement: All members in Leader MUST be locally verified,
    // but Leader only needs >= 80% of total members
    uint32_t matched_count = 0;
    uint32_t leader_member_count = 0;  // Count non-empty members from leader
    // First, reconstruct leader's common public key object for comparison
    std::vector<std::string> leader_cpk_str = {
        leader_common_pk.x_c0(),
        leader_common_pk.x_c1(),
        leader_common_pk.y_c0(),
        leader_common_pk.y_c1()
    };

    BLSPublicKey leader_common_pkey(std::make_shared<std::vector<std::string>>(leader_cpk_str));
    auto leader_common_pk_obj = *leader_common_pkey.getPublicKey();
    for (int32_t i = 0; i < ec_block.prev_members().bls_pubkey_size(); ++i) {
        const auto& leader_bls_pk = ec_block.prev_members().bls_pubkey(i);
        
        // Empty leader key means this member didn't participate from leader's side
        if (leader_bls_pk.x_c0().empty()) {
            continue;
        }
        
        ++leader_member_count;
        
        // CRITICAL: Member MUST be in our verified list (no exceptions)
        if (i >= static_cast<int32_t>(n) || !finish_item->verified[i]) {
            BLS_ERROR("[CheckBLS] net %u: member %d in leader but NOT in local verified list!", 
                      network_id, i);
            return kBlsError;  // Fail immediately - Leader has unverified member
        }
        
        // Reconstruct leader's BLS public key and compare with local
        std::vector<std::string> leader_pk_str = {
            leader_bls_pk.x_c0(),
            leader_bls_pk.x_c1(),
            leader_bls_pk.y_c0(),
            leader_bls_pk.y_c1()
        };
        
        BLSPublicKey leader_pkey(std::make_shared<std::vector<std::string>>(leader_pk_str));
        auto leader_pk_obj = *leader_pkey.getPublicKey();
        
        // Compare individual member public key
        if (finish_item->all_public_keys[i] != leader_pk_obj) {
            BLS_ERROR("[CheckBLS] net %u: member %d public key mismatch!", network_id, i);
            return kBlsError;  // Fail immediately - Key mismatch
        }
        
        // Compare common public key stored at this member
        if (finish_item->all_common_public_keys[i] != leader_common_pk_obj) {
            BLS_ERROR("[CheckBLS] net %u: member %d common public key mismatch!", network_id, i);
            return kBlsError;  // Fail immediately - CPK mismatch
        }
        
        ++matched_count;
    }
    
    // Calculate required count (80% of total members)
    uint32_t required_count = (n * 80 + 99) / 100;  // Ceiling division for 80%
    
    // All matched members must equal leader member count (all leader members verified successfully)
    if (matched_count != leader_member_count) {
        BLS_ERROR("[CheckBLS] net %u: matched=%u != leader_member_count=%u (verification failed)",
                  network_id, matched_count, leader_member_count);
        return kBlsError;
    }
    
    SHARDORA_DEBUG("[CheckBLS] net %u: leader_members=%u, all_verified, required=80%% of %u (%u), status=%s",
              network_id, leader_member_count, n, required_count,
              (leader_member_count >= required_count) ? "SUCCESS" : "FAILED");
    if (leader_member_count >= required_count) {
        return kBlsSuccess;
    }
    
    return kBlsError;
}

int BlsManager::AddBlsConsensusInfo(elect::protobuf::ElectBlock& ec_block) {
    auto iter = finish_networks_map_.find(ec_block.shard_network_id());
    if (iter == finish_networks_map_.end()) {
        BLS_ERROR("find finish_networks_map_ failed![%u]", ec_block.shard_network_id());
        return kBlsError;
    }

    if (!iter->second->success_verified) {
        BLS_ERROR("success_verified failed![%u]", ec_block.shard_network_id());
        return kBlsError;
    }

    auto elect_iter = elect_members_.find(ec_block.shard_network_id());
    if (elect_iter == elect_members_.end()) {
        return kBlsError;
    }

    auto members = elect_iter->second->members;
    if (members == nullptr) {
        BLS_ERROR("get waiting members failed![%u]", ec_block.shard_network_id());
        return kBlsError;
    }

    // At least so many nodes are required to successfully exchange keys
    auto exchange_member_count = (uint32_t)((float)members->size() * kBlsMaxExchangeMembersRatio);
    if (exchange_member_count < members->size()) {
        ++exchange_member_count;
    }

    auto t = common::GetSignerCount(members->size());
    BlsFinishItemPtr finish_item = iter->second;
    if (finish_item->max_finish_count < exchange_member_count) {
        BLS_ERROR("network: %u, finish_item->max_finish_count < t[%u][%u]",
            ec_block.shard_network_id(),
            finish_item->max_finish_count, exchange_member_count);
        return kBlsError;
    }

    auto item_iter = finish_item->max_bls_members.find(finish_item->max_finish_hash);
    if (item_iter == finish_item->max_bls_members.end()) {
        BLS_ERROR("finish_item->max_bls_members failed");
        return kBlsError;
    }

    uint32_t max_mem_size = item_iter->second->bitmap.data().size() * 64;
    if (max_mem_size < members->size()) {
        BLS_ERROR("max_mem_size < members->size()[%u][%u]", max_mem_size, members->size());
        return kBlsError;
    }

    uint32_t max_cpk_count = 0;
    std::string max_cpk_hash;
    for (auto max_cpk_count_iter = finish_item->max_public_pk_map.begin();
        max_cpk_count_iter != finish_item->max_public_pk_map.end(); ++max_cpk_count_iter) {
        if (max_cpk_count_iter->second > max_cpk_count) {
            max_cpk_count = max_cpk_count_iter->second;
            max_cpk_hash = max_cpk_count_iter->first;
        }
    }

    auto common_pk_iter = finish_item->common_pk_map.find(max_cpk_hash);
    if (common_pk_iter == finish_item->common_pk_map.end()) {
        BLS_ERROR("finish_item->common_pk_map failed!");
        return kBlsError;
    }

    //assert(ec_block.prev_members().bls_pubkey_size() == 0);
    auto pre_ec_members = ec_block.mutable_prev_members();
    for (size_t i = 0; i < members->size(); ++i) {
        auto mem_bls_pk = pre_ec_members->add_bls_pubkey();
        do {
            if (!item_iter->second->bitmap.Valid(i)) {
                mem_bls_pk->set_x_c0("");
                mem_bls_pk->set_x_c1("");
                mem_bls_pk->set_y_c0("");
                mem_bls_pk->set_y_c1("");
                BLS_WARN("0 invalid bitmap: %d", i);
                break;
            }

            if (finish_item->all_public_keys[i] == libff::alt_bn128_G2::zero()) {
                mem_bls_pk->set_x_c0("");
                mem_bls_pk->set_x_c1("");
                mem_bls_pk->set_y_c0("");
                mem_bls_pk->set_y_c1("");
                BLS_WARN("0 invalid all_public_keys: %d", i);
                break;
            }

            if (finish_item->all_common_public_keys[i] != common_pk_iter->second) {
                mem_bls_pk->set_x_c0("");
                mem_bls_pk->set_x_c1("");
                mem_bls_pk->set_y_c0("");
                mem_bls_pk->set_y_c1("");
                BLS_WARN("0 invalid all_common_public_keys: %d, %s, %s", i,
                libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].X.c0).c_str(),
                libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.X.c0).c_str());
                break;
            }

            finish_item->all_public_keys[i].to_affine_coordinates();
            mem_bls_pk->set_x_c0(
                libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].X.c0));
            mem_bls_pk->set_x_c1(
                libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].X.c1));
            mem_bls_pk->set_y_c0(
                libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].Y.c0));
            mem_bls_pk->set_y_c1(
                libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].Y.c1));
        } while (0);

        if (mem_bls_pk->x_c0() == "") {
            SHARDORA_ERROR("member index: %d, bls pk is empty!", i);
        }
    }

    //assert(static_cast<size_t>(ec_block.prev_members().bls_pubkey_size()) == members->size());
    common_pk_iter->second.to_affine_coordinates();
    auto common_pk = pre_ec_members->mutable_common_pubkey();
    common_pk->set_x_c0(
        libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.X.c0));
    common_pk->set_x_c1(
        libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.X.c1));
    common_pk->set_y_c0(
        libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.Y.c0));
    common_pk->set_y_c1(
        libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.Y.c1));
    pre_ec_members->set_prev_elect_height(elect_iter->second->height);
    // ResetLeaders(members, ec_block.mutable_prev_members());
//     SHARDORA_WARN("network: %u, elect height: %lu, AddBlsConsensusInfo success max_finish_count_: %d,"
//         "member count: %d, x_c0: %s, x_c1: %s, y_c0: %s, y_c1: %s.",
//         ec_block.shard_network_id(),
//         elect_iter->second->height,
//         item_iter->second->bitmap.valid_count(), members->size(),
//         common_pk->x_c0().c_str(), common_pk->x_c1().c_str(),
//         common_pk->y_c0().c_str(), common_pk->y_c1().c_str());
    return kBlsSuccess;
}

void BlsManager::SyncFinishMessageToNeighbors(uint32_t network_id) {
    // Check if we have finish item for this network
    auto finish_iter = finish_networks_map_.find(network_id);
    if (finish_iter == finish_networks_map_.end()) {
        return;
    }

    BlsFinishItemPtr finish_item = finish_iter->second;
    
    // Check if we have elect members for this network
    auto elect_iter = elect_members_.find(network_id);
    if (elect_iter == elect_members_.end()) {
        return;
    }

    auto members = elect_iter->second->members;
    if (!members) {
        return;
    }

    uint32_t n = static_cast<uint32_t>(members->size());
    uint32_t t = (n + 1) / 2;  // 1/2 threshold for sync

    // Check if we are in the finish period
    auto waiting_bls = LoadWaitingBls();
    if (!waiting_bls) {
        return;
    }

    if (!waiting_bls->IsFinishPeriod()) {
        return;
    }

    // Count how many finish messages we have received
    uint32_t verified_count = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (finish_item->verified[i]) {
            ++verified_count;
        }
    }

    // Prerequisite: Must have received at least 1/2 finish messages
    if (verified_count < t) {
        return;
    }

    // Get local member index
    uint32_t local_member_index = common::kInvalidUint32;
    std::string local_id = security_->GetAddress();
    for (uint32_t i = 0; i < members->size(); ++i) {
        if ((*members)[i]->id == local_id) {
            local_member_index = i;
            break;
        }
    }

    if (local_member_index == common::kInvalidUint32) {
        return;
    }

    // Get max finish hash
    if (finish_item->max_finish_hash.empty()) {
        return;
    }

    // Find the bitmap for max finish hash
    auto max_bls_iter = finish_item->max_bls_members.find(finish_item->max_finish_hash);
    if (max_bls_iter == finish_item->max_bls_members.end()) {
        return;
    }

    // Identify missing nodes (nodes we haven't verified yet)
    std::vector<uint32_t> missing_nodes;
    for (uint32_t i = 0; i < n; ++i) {
        if (!finish_item->verified[i]) {
            missing_nodes.push_back(i);
        }
    }

    if (missing_nodes.empty()) {
        return;
    }

    // Request missing finish messages from neighbors
    // Strategy: Ask neighbors who have verified for the missing nodes' finish messages
    uint32_t sync_count = 0;
    uint32_t max_neighbors = std::min(8u, n);  // Ask at most 8 neighbors

    for (uint32_t neighbor_offset = 1; neighbor_offset <= max_neighbors && sync_count < missing_nodes.size(); ++neighbor_offset) {
        uint32_t neighbor_idx = (local_member_index + neighbor_offset) % n;
        if (neighbor_idx == local_member_index) {
            continue;
        }

        // Check if this neighbor has verified their finish message
        if (!finish_item->verified[neighbor_idx]) {
            continue;
        }

        // Request missing nodes' finish messages from this neighbor
        // Create a sync request message
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = msg_ptr->header;
        msg.set_src_sharding_id(network_id);
        msg.set_type(common::kBlsMessage);
        msg.set_hop_count(0);
        msg.set_des_dht_key((*members)[neighbor_idx]->id);
        
        auto& bls_msg = *msg.mutable_bls_proto();
        bls_msg.set_index(local_member_index);
        bls_msg.set_elect_height(elect_iter->second->height);
        
        // Add a sync request with missing node indices
        auto sync_req = bls_msg.mutable_finish_sync_req();
        sync_req->set_network_id(network_id);
        for (uint32_t missing_idx : missing_nodes) {
            sync_req->add_missing_indices(missing_idx);
        }
        
        network::Route::Instance()->Send(msg_ptr);
        ++sync_count;
    }
}

void BlsManager::ResetLeaders(
        const common::MembersPtr& members,
        elect::protobuf::PrevMembers* prev_members) {
    for (int32_t i = 0; i < prev_members->bls_pubkey_size(); ++i) {
        if ((*members)[i]->pool_index_mod_num >= 0) {
            auto bls_pk = prev_members->mutable_bls_pubkey(i);
            if (bls_pk->x_c0().empty()) {
                for (uint32_t mem_idx = 0; mem_idx < members->size(); ++mem_idx) {
                    if ((*members)[mem_idx]->pool_index_mod_num < 0) {
                        (*members)[mem_idx]->pool_index_mod_num.store((*members)[i]->pool_index_mod_num);
                        auto prev_bls_pk = prev_members->mutable_bls_pubkey(mem_idx);
                        prev_bls_pk->set_pool_idx_mod_num((*members)[i]->pool_index_mod_num);
                        break;
                    }
                }
            } else {
                bls_pk->set_pool_idx_mod_num((*members)[i]->pool_index_mod_num);
            }
        }
    }
}

};  // namespace bls

};  // namespace shardora