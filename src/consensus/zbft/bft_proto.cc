#include "consensus/zbft/bft_proto.h"

#include "consensus/zbft/zbft_utils.h"
#include "bls/bls_manager.h"
#include "common/global_info.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "transport/tcp_transport.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace consensus {

bool BftProto::LeaderCreatePrepare(
        const ZbftPtr& bft_ptr,
        const std::string& precommit_gid,
        const std::string& commit_gid,
        transport::protobuf::Header& msg,
        zbft::protobuf::ZbftMessage* pipeline_msg) {
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *pipeline_msg;
    bft_msg.set_leader(false);
    bft_msg.set_prepare_gid(bft_ptr->gid());
    bft_msg.set_precommit_gid(precommit_gid);
    bft_msg.set_commit_gid(commit_gid);
//     ZJC_DEBUG("leader prepare: %s, precommit: %s, commit: %s",
//         common::Encode::HexEncode(bft_ptr->gid()).c_str(),
//         common::Encode::HexEncode(precommit_gid).c_str(),
//         common::Encode::HexEncode(commit_gid).c_str());
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_elect_height(bft_ptr->elect_height());
    auto prev_btr = bft_ptr->pipeline_prev_zbft_ptr();
    if (prev_btr != nullptr) {
        const auto& bitmap_data = prev_btr->prepare_bitmap().data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            bft_msg.add_bitmap(bitmap_data[i]);
        }

        auto& bls_precommit_sign = prev_btr->bls_precommit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->Y));
    }

    return true;
}

bool BftProto::BackupCreatePrepare(
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const ZbftPtr& bft_ptr,
        bool agree,
        const std::string& precommit_gid,
        zbft::protobuf::ZbftMessage* pipeline_msg) {
    auto& bft_msg = *pipeline_msg;
    bft_msg.set_leader(true);
    bft_msg.set_prepare_gid(bft_ptr->gid());
    bft_msg.set_precommit_gid(precommit_gid);
    bft_msg.set_prepare_hash(bft_ptr->local_prepare_hash());
    std::string bls_sign_x;
    std::string bls_sign_y;
    if (bls_mgr->Sign(
            bft_ptr->min_aggree_member_count(),
            bft_ptr->member_count(),
            bft_ptr->local_sec_key(),
            bft_ptr->g1_prepare_hash(),
            &bls_sign_x,
            &bls_sign_y) != bls::kBlsSuccess) {
        return false;
    }

    bft_msg.set_bls_sign_x(bls_sign_x);
    bft_msg.set_bls_sign_y(bls_sign_y);
    return true;
}

bool BftProto::LeaderCreatePreCommit(
        const ZbftPtr& bft_ptr,
        bool agree,
        const std::string& commit_gid,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *msg.mutable_zbft();
    bft_msg.set_leader(false);
    bft_msg.set_precommit_gid(bft_ptr->gid());
    bft_msg.set_commit_gid(commit_gid);
//     ZJC_DEBUG("leader precommit: %s, precommit: %s, commit: %s",
//         common::Encode::HexEncode(bft_msg.prepare_gid()).c_str(),
//         common::Encode::HexEncode(bft_ptr->gid()).c_str(),
//         common::Encode::HexEncode(commit_gid).c_str());
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_agree_precommit(agree);
    bft_msg.set_agree_commit(agree);
    bft_msg.set_elect_height(bft_ptr->elect_height());
    auto pre_ptr = bft_ptr->pipeline_prev_zbft_ptr();
    if (agree && pre_ptr != nullptr) {
        bft_msg.set_prepare_hash(pre_ptr->local_prepare_hash());
    }

    if (agree) {
        const auto& bitmap_data = bft_ptr->prepare_bitmap().data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            bft_msg.add_bitmap(bitmap_data[i]);
        }

        auto& bls_precommit_sign = bft_ptr->bls_precommit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->Y));

        if (bft_ptr->is_synced_block()) {
            auto& valid_idx = bft_ptr->valid_index();
            for (uint32_t i = 0; i < valid_idx.size(); ++i) {
                bft_msg.add_sync_block_members(valid_idx[i]);
            }
        }
    }

    return true;
}

bool BftProto::BackupCreatePreCommit(
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg) {
    auto& bft_msg = *msg.mutable_zbft();
    bft_msg.set_leader(true);
    bft_msg.set_precommit_gid(bft_ptr->gid());
    std::string bls_sign_x;
    std::string bls_sign_y;
    if (bls_mgr->Sign(
            bft_ptr->min_aggree_member_count(),
            bft_ptr->member_count(),
            bft_ptr->local_sec_key(),
            bft_ptr->g1_precommit_hash(),
            &bls_sign_x,
            &bls_sign_y) != bls::kBlsSuccess) {
        return false;
    }

    bft_msg.set_bls_sign_x(bls_sign_x);
    bft_msg.set_bls_sign_y(bls_sign_y);
    return true;
}

bool BftProto::LeaderCreateCommit(
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *msg.mutable_zbft();
    zbft::protobuf::TxBft& tx_bft = *bft_msg.mutable_tx_bft();
    bft_msg.set_leader(false);
    bft_msg.set_commit_gid(bft_ptr->gid());
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_agree_commit(agree);
    const auto& bitmap_data = bft_ptr->prepare_bitmap().data();
    for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
        bft_msg.add_bitmap(bitmap_data[i]);
    }

    std::string hash_to_sign = bft_ptr->prpare_block()->hash();
    if (agree) {
        auto& bls_commit_sign = bft_ptr->bls_commit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_commit_sign->Y));
        std::string msg_hash_src = bft_ptr->precommit_hash();
        const auto& commit_bitmap_data = bft_ptr->precommit_bitmap().data();
        for (uint32_t i = 0; i < commit_bitmap_data.size(); ++i) {
            bft_msg.add_commit_bitmap(commit_bitmap_data[i]);
            msg_hash_src += std::to_string(commit_bitmap_data[i]);
        }
    }

    bft_msg.set_prepare_hash(bft_ptr->local_prepare_hash());
    return true;
}

}  // namespace consensus

}  // namespace zjchain
