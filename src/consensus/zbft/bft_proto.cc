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

void BftProto::SetLocalPublicIpPort(
        const dht::NodePtr& local_node,
        hotstuff::protobuf::HotstuffMessage& bft_msg) {
    if (common::GlobalInfo::Instance()->config_first_node()) {
        common::Split<> spliter(
            common::GlobalInfo::Instance()->tcp_spec().c_str(),
            ':',
            common::GlobalInfo::Instance()->tcp_spec().size());
        if (spliter.Count() == 2) {
            bft_msg.set_node_ip(spliter[0]);
            uint16_t port = 0;
            common::StringUtil::ToUint16(spliter[1], &port);
            bft_msg.set_node_port(port);
        }
    } else {
        bft_msg.set_node_ip(local_node->public_ip);
        bft_msg.set_node_port(local_node->public_port + 1);
    }
}

bool BftProto::LeaderCreatePrepare(
        std::shared_ptr<security::Security>& security_ptr,
        const ZbftPtr& bft_ptr,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    msg.set_hop_count(0);
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *msg.mutable_hotstuff_proto();
    bft_msg.set_leader(false);
    bft_msg.set_gid(bft_ptr->gid());
    bft_msg.set_net_id(bft_ptr->network_id());
    bft_msg.set_bft_step(kConsensusPrepare);
    bft_msg.set_agree(true);
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_epoch(bft_ptr->GetEpoch());
    bft_msg.set_member_index(bft_ptr->local_member_index());
    bft_msg.set_elect_height(bft_ptr->elect_height());
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    if (security_ptr->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        return false;
    }

    msg.set_sign(sign);
    return true;
}

bool BftProto::BackupCreatePrepare(
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const transport::protobuf::Header& from_header,
        const hotstuff::protobuf::HotstuffMessage& from_bft_msg,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto& bft_msg = *msg.mutable_hotstuff_proto();
    bft_msg.set_leader(true);
    bft_msg.set_gid(from_bft_msg.gid());
    bft_msg.set_net_id(from_bft_msg.net_id());
    bft_msg.set_agree(agree);
    bft_msg.set_bft_step(kConsensusPrepare);
    bft_msg.set_epoch(from_bft_msg.epoch());
    bft_msg.set_member_index(bft_ptr->local_member_index());
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
    auto bls_sign_hash = common::Hash::keccak256(bls_sign_x + bls_sign_y);
    std::string& ecdh_key = bft_ptr->leader_mem_ptr()->peer_ecdh_key;
    if (ecdh_key.empty()) {
        if (security_ptr->GetEcdhKey(
                bft_ptr->leader_mem_ptr()->pubkey,
                &ecdh_key) != security::kSecuritySuccess) {
            ZJC_ERROR("get ecdh key failed peer pk: %s",
                common::Encode::HexEncode(bft_ptr->leader_mem_ptr()->pubkey).c_str());
            return false;
        }
    }

    std::string enc_out;
    if (security_ptr->Encrypt(bls_sign_hash, ecdh_key, &enc_out) != security::kSecuritySuccess) {
        ZJC_ERROR("encrypt key failed peer pk: %s",
            common::Encode::HexEncode(bft_ptr->leader_mem_ptr()->pubkey).c_str());
        return false;
    }
    
    bft_msg.set_backup_enc_data(enc_out);
    return true;
}

bool BftProto::LeaderCreatePreCommit(
        std::shared_ptr<security::Security>& security_ptr,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *msg.mutable_hotstuff_proto();
    bft_msg.set_leader(false);
    bft_msg.set_gid(bft_ptr->gid());
    bft_msg.set_net_id(bft_ptr->network_id());
    bft_msg.set_bft_step(kConsensusPreCommit);
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_agree(agree);
    bft_msg.set_elect_height(bft_ptr->elect_height());
    bft_msg.set_member_index(bft_ptr->local_member_index());
    bft_msg.set_epoch(bft_ptr->GetEpoch());
    if (agree) {
        const auto& bitmap_data = bft_ptr->prepare_bitmap().data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            bft_msg.add_bitmap(bitmap_data[i]);
        }

        auto& bls_precommit_sign = bft_ptr->bls_precommit_agg_sign();
        bft_msg.set_bls_sign_x(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->X));
        bft_msg.set_bls_sign_y(libBLS::ThresholdUtils::fieldElementToString(bls_precommit_sign->Y));
        bft_msg.set_bls_agg_verify_hash(bft_ptr->precommit_bls_agg_verify_hash());
    }

    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    if (security_ptr->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        return false;
    }

    msg.set_sign(sign);
    return true;
}

bool BftProto::BackupCreatePreCommit(
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const transport::protobuf::Header& from_header,
        const hotstuff::protobuf::HotstuffMessage& from_bft_msg,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto& bft_msg = *msg.mutable_hotstuff_proto();
    bft_msg.set_leader(true);
    bft_msg.set_gid(from_bft_msg.gid());
    bft_msg.set_net_id(from_bft_msg.net_id());
    bft_msg.set_agree(agree);
    bft_msg.set_bft_step(kConsensusPreCommit);
    bft_msg.set_epoch(from_bft_msg.epoch());
    bft_msg.set_member_index(bft_ptr->local_member_index());
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
    auto bls_sign_hash = common::Hash::keccak256(bls_sign_x + bls_sign_y);
    std::string& ecdh_key = bft_ptr->leader_mem_ptr()->peer_ecdh_key;
    if (ecdh_key.empty()) {
        if (security_ptr->GetEcdhKey(
            bft_ptr->leader_mem_ptr()->pubkey,
            &ecdh_key) != security::kSecuritySuccess) {
            ZJC_ERROR("get ecdh key failed peer pk: %s",
                common::Encode::HexEncode(bft_ptr->leader_mem_ptr()->pubkey).c_str());
            return false;
        }

    }

    std::string enc_out;
    if (security_ptr->Encrypt(bls_sign_hash, ecdh_key, &enc_out) != security::kSecuritySuccess) {
        ZJC_ERROR("encrypt key failed peer pk: %s",
            common::Encode::HexEncode(bft_ptr->leader_mem_ptr()->pubkey).c_str());
        return false;
    }

    bft_msg.set_backup_enc_data(enc_out);
    return true;
}

bool BftProto::LeaderCreateCommit(
        std::shared_ptr<security::Security>& security_ptr,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(bft_ptr->network_id());
    dht::DhtKeyManager dht_key(bft_ptr->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *msg.mutable_leader_commit();
    hotstuff::protobuf::TxBft& tx_bft = *bft_msg.mutable_tx_bft();
    auto ltx_commit_msg = tx_bft.mutable_ltx_commit();
    ltx_commit_msg->set_latest_hegight(bft_ptr->prepare_latest_height());
    bft_msg.set_leader(false);
    bft_msg.set_gid(bft_ptr->gid());
    bft_msg.set_net_id(bft_ptr->network_id());
    bft_msg.set_bft_step(kConsensusCommit);
    bft_msg.set_pool_index(bft_ptr->pool_index());
    bft_msg.set_member_index(bft_ptr->local_member_index());
    bft_msg.set_agree(agree);
    const auto& bitmap_data = bft_ptr->prepare_bitmap().data();
    for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
        bft_msg.add_bitmap(bitmap_data[i]);
    }

    std::string hash_to_sign = bft_ptr->leader_tbft_prepare_hash();
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

        bft_msg.set_bls_agg_verify_hash(bft_ptr->commit_bls_agg_verify_hash());
    }

    bft_msg.set_prepare_hash(bft_ptr->leader_tbft_prepare_hash());
    bft_msg.set_epoch(bft_ptr->GetEpoch());
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    if (security_ptr->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        return false;
    }

    msg.set_sign(sign);
    return true;
}

bool BftProto::CreateLeaderBroadcastToAccount(
        uint32_t net_id,
        uint32_t message_type,
        uint32_t bft_step,
        bool universal,
        const std::shared_ptr<block::protobuf::Block>& block_ptr,
        uint32_t local_member_index,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(net_id);
    dht::DhtKeyManager dht_key(net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kConsensusMessage);
    auto broad_param = msg.mutable_broadcast();
    auto& bft_msg = *msg.mutable_hotstuff_proto();
    hotstuff::protobuf::TxBft& tx_bft = *bft_msg.mutable_tx_bft();
    auto to_tx = tx_bft.mutable_to_tx();
    auto block = to_tx->mutable_block();
    *block = *(block_ptr.get());
    bft_msg.set_bft_step(bft_step);
    bft_msg.set_net_id(common::GlobalInfo::Instance()->network_id());
    bft_msg.set_member_index(local_member_index);
    auto block_hash = GetBlockHash(*block);
    block->set_hash(block_hash);
//     security::Signature sign;
//     bool sign_res = security::Security::Instance()->Sign(
//         block_hash,
//         *(security::Security::Instance()->prikey()),
//         *(security::Security::Instance()->pubkey()),
//         sign);
//     if (!sign_res) {
//         ZJC_ERROR("signature error.");
//         return;
//     }
// 
//     std::string sign_challenge_str;
//     std::string sign_response_str;
//     sign.Serialize(sign_challenge_str, sign_response_str);
//     bft_msg.set_sign_challenge(sign_challenge_str);
//     bft_msg.set_sign_response(sign_response_str);
    return true;
}

}  // namespace consensus

}  // namespace zjchain
