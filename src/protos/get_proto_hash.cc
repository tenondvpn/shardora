#include "protos/get_proto_hash.h"

#include "common/encode.h"
#include "common/hash.h"
#include "common/unique_map.h"
#include "pools/tx_utils.h"
#include "protos/address.pb.h"
#include "protos/vss.pb.h"
#include "protos/zbft.pb.h"

namespace shardora {

namespace protos {


static void GetTxProtoHash(
        const pools::protobuf::TxMessage& pools_msg,
        std::string* msg) {
    std::string& msg_for_hash = *msg;
    msg_for_hash.reserve(1024);
    msg_for_hash.append(pools_msg.gid());
    msg_for_hash.append(pools_msg.pubkey());
    uint64_t gas_limit = pools_msg.gas_limit();
    msg_for_hash.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = pools_msg.gas_price();
    msg_for_hash.append(std::string((char*)&gas_price, sizeof(gas_price)));
    auto type = pools_msg.step();
    msg_for_hash.append(std::string((char*)&type, sizeof(type)));
    msg_for_hash.append(pools_msg.key());
    msg_for_hash.append(pools_msg.value());
    msg_for_hash.append(pools_msg.to());
    uint64_t amount = pools_msg.amount();
    msg_for_hash.append(std::string((char*)&amount, sizeof(amount)));
}

static void GetZbftHash(
        const zbft::protobuf::ZbftMessage& zbft_msg,
        std::string* msg) {
    std::string& msg_for_hash = *msg;
    msg_for_hash.reserve(1024);
    if (zbft_msg.has_prepare_gid()) {
        msg_for_hash.append(zbft_msg.prepare_gid());
    }

    if (zbft_msg.has_precommit_gid()) {
        msg_for_hash.append(zbft_msg.precommit_gid());
    }

    if (zbft_msg.has_commit_gid()) {
        msg_for_hash.append(zbft_msg.commit_gid());
    }

    int32_t leader = zbft_msg.leader_idx();
    msg_for_hash.append((char*)&leader, sizeof(leader));
    if (zbft_msg.has_net_id()) {
        uint32_t net_id = zbft_msg.net_id();
        msg_for_hash.append((char*)&net_id, sizeof(net_id));
    }

    if (zbft_msg.has_agree_precommit()) {
        bool agree_precommit = zbft_msg.agree_precommit();
        msg_for_hash.append((char*)&agree_precommit, sizeof(agree_precommit));
    }

    if (zbft_msg.has_agree_commit()) {
        bool agree_commit = zbft_msg.agree_commit();
        msg_for_hash.append((char*)&agree_commit, sizeof(agree_commit));
    }

    if (zbft_msg.has_pool_index()) {
        uint32_t pool_index = zbft_msg.pool_index();
        msg_for_hash.append((char*)&pool_index, sizeof(pool_index));
    }

    if (zbft_msg.has_error()) {
        int32_t error = zbft_msg.error();
        msg_for_hash.append((char*)&error, sizeof(error));
    }

    if (zbft_msg.has_tx_bft()) {
        if (zbft_msg.tx_bft().has_prepare_final_hash()) {
            msg_for_hash.append(zbft_msg.tx_bft().prepare_final_hash());
        }

        if (zbft_msg.tx_bft().has_height()) {
            uint64_t height = zbft_msg.tx_bft().height();
            msg_for_hash.append((char*)&height, sizeof(height));
        }

        if (zbft_msg.tx_bft().txs_size() > 0) {
            for (int32_t i = 0; i < zbft_msg.tx_bft().txs_size(); ++i) {
                auto txhash = pools::GetTxMessageHash(zbft_msg.tx_bft().txs(i));
                msg_for_hash.append(txhash);
            }
        }

        if (zbft_msg.tx_bft().has_tx_type()) {
            int32_t tx_type = zbft_msg.tx_bft().tx_type();
            msg_for_hash.append((char*)&tx_type, sizeof(tx_type));
        }
    }

    if (zbft_msg.has_member_index()) {
        uint32_t member_index = zbft_msg.member_index();
        msg_for_hash.append((char*)&member_index, sizeof(member_index));
    }

    if (zbft_msg.has_elect_height()) {
        uint64_t elect_height = zbft_msg.elect_height();
        msg_for_hash.append((char*)&elect_height, sizeof(elect_height));
    }

    if (zbft_msg.has_bls_sign_y()) {
        msg_for_hash.append(zbft_msg.bls_sign_y());
    }

    if (zbft_msg.has_bls_sign_y()) {
        msg_for_hash.append(zbft_msg.bls_sign_y());
    }

    if (zbft_msg.has_prepare_hash()) {
        msg_for_hash.append(zbft_msg.prepare_hash());
    }
}

static void GetVssHash(
        const vss::protobuf::VssMessage& vss_msg,
        std::string* msg) {
    std::string msg_for_hash;
    msg_for_hash.reserve(128);
    if (vss_msg.has_random_hash()) {
        uint64_t random_hash = vss_msg.random_hash();
        msg_for_hash.append((char*)&random_hash, sizeof(random_hash));
    }

    if (vss_msg.has_random()) {
        uint64_t random = vss_msg.random();
        msg_for_hash.append((char*)&random, sizeof(random));
    }

    uint32_t mem_idx = vss_msg.member_index();
    msg_for_hash.append((char*)&mem_idx, sizeof(mem_idx));
    uint64_t tm_height = vss_msg.tm_height();
    msg_for_hash.append((char*)&tm_height, sizeof(tm_height));
    uint64_t elect_height = vss_msg.elect_height();
    msg_for_hash.append((char*)&elect_height, sizeof(elect_height));
    int32_t type = vss_msg.type();
    msg_for_hash.append((char*)&type, sizeof(type));
    *msg = common::Hash::keccak256(msg_for_hash);
}

static void GetBlsHash(
        const bls::protobuf::BlsMessage& bls_msg,
        std::string* msg) {
    std::string msg_for_hash;
    bool has_filed = false;
    msg_for_hash.reserve(1024 * 1024);
    if (bls_msg.has_verify_brd()) {
        for (int32_t i = 0; i < bls_msg.verify_brd().verify_vec_size(); ++i) {
            msg_for_hash.append(bls_msg.verify_brd().verify_vec(i).x_c0());
            msg_for_hash.append(bls_msg.verify_brd().verify_vec(i).x_c1());
            msg_for_hash.append(bls_msg.verify_brd().verify_vec(i).y_c0());
            msg_for_hash.append(bls_msg.verify_brd().verify_vec(i).y_c1());
            msg_for_hash.append(bls_msg.verify_brd().verify_vec(i).z_c0());
            msg_for_hash.append(bls_msg.verify_brd().verify_vec(i).z_c1());
        }
    } else {
        msg_for_hash.append((char*)&has_filed, sizeof(has_filed));
    }

    if (bls_msg.has_swap_req()) {
        for (int32_t i = 0; i < bls_msg.swap_req().keys_size(); ++i) {
            msg_for_hash.append(bls_msg.swap_req().keys(i).sec_key());
            uint32_t sec_key_len = bls_msg.swap_req().keys(i).sec_key_len();
            msg_for_hash.append((char*)&sec_key_len, sizeof(sec_key_len));
        }
    } else {
        msg_for_hash.append((char*)&has_filed, sizeof(has_filed));
    }

    if (bls_msg.has_finish_req()) {
        for (int32_t i = 0; i < bls_msg.finish_req().bitmap_size(); ++i) {
            uint64_t bitmap_data = bls_msg.finish_req().bitmap(i);
            msg_for_hash.append((char*)&bitmap_data, sizeof(bitmap_data));
        }

        if (bls_msg.finish_req().has_pubkey()) {
            msg_for_hash.append(bls_msg.finish_req().pubkey().x_c0());
            msg_for_hash.append(bls_msg.finish_req().pubkey().x_c1());
            msg_for_hash.append(bls_msg.finish_req().pubkey().y_c0());
            msg_for_hash.append(bls_msg.finish_req().pubkey().y_c1());
        } else {
            msg_for_hash.append((char*)&has_filed, sizeof(has_filed));
        }

        if (bls_msg.finish_req().has_common_pubkey()) {
            msg_for_hash.append(bls_msg.finish_req().common_pubkey().x_c0());
            msg_for_hash.append(bls_msg.finish_req().common_pubkey().x_c1());
            msg_for_hash.append(bls_msg.finish_req().common_pubkey().y_c0());
            msg_for_hash.append(bls_msg.finish_req().common_pubkey().y_c1());
        } else {
            msg_for_hash.append((char*)&has_filed, sizeof(has_filed));
        }
    } else {
        msg_for_hash.append((char*)&has_filed, sizeof(has_filed));
    }

    uint32_t index = bls_msg.index();
    msg_for_hash.append((char*)&index, sizeof(index));
    uint64_t elect_height = bls_msg.elect_height();
    msg_for_hash.append((char*)&elect_height, sizeof(elect_height));
    *msg = common::Hash::keccak256(msg_for_hash);
}

void GetProtoHash(const transport::protobuf::Header& msg, std::string* msg_for_hash) {
    if (msg.has_tx_proto()) {
        GetTxProtoHash(msg.tx_proto(), msg_for_hash);
    } else if (msg.has_zbft()) {
        GetZbftHash(msg.zbft(), msg_for_hash);
    } else if (msg.has_vss_proto()) {
        GetVssHash(msg.vss_proto(), msg_for_hash);
    } else if (msg.has_bls_proto()) {
        GetBlsHash(msg.bls_proto(), msg_for_hash);
    }
}

std::string GetElectBlockHash(const elect::protobuf::ElectBlock& elect_block) {
    std::string string_for_hash;
    string_for_hash.reserve(1024 * 1024);
    for (int32_t i = 0; i < elect_block.in_size(); ++i) {
        string_for_hash.append(elect_block.in(i).pubkey());
        uint32_t mod_num = elect_block.in(i).pool_idx_mod_num();
        string_for_hash.append((char*)&mod_num, sizeof(mod_num));
        ZJC_DEBUG("in: %s, %d", common::Encode::HexEncode(elect_block.in(i).pubkey()).c_str(), mod_num);
    }

    if (elect_block.has_prev_members()) {
        for (int32_t i = 0; i < elect_block.prev_members().bls_pubkey_size(); ++i) {
            auto& pk = elect_block.prev_members().bls_pubkey(i);
            string_for_hash.append(pk.x_c0());
            string_for_hash.append(pk.x_c1());
            string_for_hash.append(pk.y_c0());
            string_for_hash.append(pk.y_c1());
            ZJC_DEBUG("pk: %s", common::Encode::HexEncode(pk.x_c0()).c_str());
        }

        auto& pk = elect_block.prev_members().common_pubkey();
        string_for_hash.append(pk.x_c0());
        string_for_hash.append(pk.x_c1());
        string_for_hash.append(pk.y_c0());
        string_for_hash.append(pk.y_c1());
        uint64_t prev_elect_height = elect_block.prev_members().prev_elect_height();
        string_for_hash.append((char*)&prev_elect_height, sizeof(prev_elect_height));
        ZJC_DEBUG("cpk: %s, prev_elect_height: %lu", common::Encode::HexEncode(pk.x_c0()).c_str(), prev_elect_height);
    }

    uint32_t shard_network_id = elect_block.shard_network_id();
    string_for_hash.append((char*)&shard_network_id, sizeof(shard_network_id));
    uint64_t elect_height = elect_block.elect_height();
    string_for_hash.append((char*)&elect_height, sizeof(elect_height));
    ZJC_DEBUG("shard_network_id: %u, elect_height: %lu, hash: %s, string: %s",
        shard_network_id, elect_height,
        common::Encode::HexEncode(common::Hash::keccak256(string_for_hash)).c_str(),
        common::Encode::HexEncode(string_for_hash).c_str());
    return common::Hash::keccak256(string_for_hash);
}

std::string GetJoinElectReqHash(const bls::protobuf::JoinElectInfo& req) {
    std::string string_for_hash;
    string_for_hash.reserve(req.g2_req().verify_vec_size() * 6 * 64 + 12);
    uint32_t shard_id = req.shard_id();
    string_for_hash.append((char*)&shard_id, sizeof(shard_id));
    uint32_t member_idx = req.member_idx();
    string_for_hash.append((char*)&member_idx, sizeof(member_idx));
    uint32_t change_idx = req.change_idx();
    string_for_hash.append((char*)&change_idx, sizeof(change_idx));
    for (int32_t i = 0; i < req.g2_req().verify_vec_size(); ++i) {
        string_for_hash.append(req.g2_req().verify_vec(i).x_c0());
        string_for_hash.append(req.g2_req().verify_vec(i).x_c1());
        string_for_hash.append(req.g2_req().verify_vec(i).y_c0());
        string_for_hash.append(req.g2_req().verify_vec(i).y_c1());
        string_for_hash.append(req.g2_req().verify_vec(i).z_c0());
        string_for_hash.append(req.g2_req().verify_vec(i).z_c1());
    }

    return common::Hash::keccak256(string_for_hash);
}


};  // namespace protos

};  // namespace shardora