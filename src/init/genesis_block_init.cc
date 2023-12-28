#include "init/genesis_block_init.h"

#include <cmath>
#include <utility>
#include <vector>

#include <libbls/tools/utils.h>

#define private public
#define protected public
#include "common/encode.h"
#include "common/global_info.h"
#include "common/random.h"
#include "common/split.h"
#include "contract/contract_manager.h"
#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_sign.h"
#include "consensus/consensus_utils.h"
#include "consensus/zbft/zbft_utils.h"
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "init/init_utils.h"
#include "pools/shard_statistic.h"
#include "pools/tx_pool_manager.h"
#include "protos/get_proto_hash.h"
#include "protos/zbft.pb.h"
#include "security/ecdsa/ecdsa.h"
#include "security/ecdsa/secp256k1.h"
#include "timeblock/time_block_utils.h"

namespace zjchain {

namespace init {

GenesisBlockInit::GenesisBlockInit(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<db::Db>& db)
        : account_mgr_(account_mgr), block_mgr_(block_mgr), db_(db) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
}

GenesisBlockInit::~GenesisBlockInit() {}

// CreateGenesisBlocks 给所有的 net 中所有的 pool 创建创世块
// 并创建创世账户（root 创世账户、shard 创世账户、root pool 账户、节点账户）
// 并创建选举块数据
int GenesisBlockInit::CreateGenesisBlocks(
    const GenisisNetworkType& net_type,
    const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
    const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards,
    const std::set<uint32_t>& valid_net_ids_set) {
    int res = kInitSuccess;    
    // 事先计算一下每个节点账户要分配的余额
    std::unordered_map<std::string, uint64_t> genesis_acount_balance_map;
    genesis_acount_balance_map = GetGenesisAccountBalanceMap(root_genesis_nodes, cons_genesis_nodes_of_shards);

    std::vector<GenisisNodeInfoPtr> real_root_genesis_nodes;
    std::vector<GenisisNodeInfoPtrVector> real_cons_genesis_nodes_of_shards(cons_genesis_nodes_of_shards.size());
    // 根据 valid_net_ids_set 去掉不处理的 nodes
    auto root_iter = valid_net_ids_set.find(network::kRootCongressNetworkId);
    if (root_iter != valid_net_ids_set.end()) {
        real_root_genesis_nodes = root_genesis_nodes;
    }
    for (uint32_t i = 0; i < cons_genesis_nodes_of_shards.size(); i++) {
        uint32_t shard_node_net_id = i + network::kConsensusShardBeginNetworkId;
        auto shard_iter = valid_net_ids_set.find(shard_node_net_id);
        if (shard_iter != valid_net_ids_set.end()) {
            real_cons_genesis_nodes_of_shards[i] = cons_genesis_nodes_of_shards[i]; 
        }
    }
    

    if (net_type == GenisisNetworkType::RootNetwork) { // 构造 root 创世网络
        // 生成节点私有数据，如 bls
        std::vector<std::string> prikeys;
        CreateNodePrivateInfo(network::kRootCongressNetworkId, 1llu, real_root_genesis_nodes);
        for (uint32_t i = 0; i < real_cons_genesis_nodes_of_shards.size(); i++) {
            uint32_t net_id = i + network::kConsensusShardBeginNetworkId;
            if (cons_genesis_nodes_of_shards[i].size() != 0) {
                CreateNodePrivateInfo(net_id, 1llu, cons_genesis_nodes_of_shards[i]);    
            }
        }
        
        common::GlobalInfo::Instance()->set_network_id(network::kRootCongressNetworkId);
        PrepareCreateGenesisBlocks();
        res = CreateRootGenesisBlocks(real_root_genesis_nodes,
                                      real_cons_genesis_nodes_of_shards,
                                      genesis_acount_balance_map);

        for (uint32_t i = 0; i < real_root_genesis_nodes.size(); ++i) {
            prikeys.push_back(real_root_genesis_nodes[i]->prikey);
        }

        ComputeG2sForNodes(prikeys);
    } else { // 构建某 shard 创世网络
        // TODO 这种写法是每个 shard 单独的 shell 命令，不适用，需要改
        for (uint32_t i = 0; i < real_cons_genesis_nodes_of_shards.size(); i++) {
            std::vector<std::string> prikeys;
            uint32_t shard_node_net_id = i + network::kConsensusShardBeginNetworkId;
            std::vector<GenisisNodeInfoPtr> cons_genesis_nodes = real_cons_genesis_nodes_of_shards[i];

            if (shard_node_net_id == 0 || cons_genesis_nodes.size() == 0) {
                continue;
            }

            CreateNodePrivateInfo(shard_node_net_id, 1llu, cons_genesis_nodes);
            common::GlobalInfo::Instance()->set_network_id(shard_node_net_id);
            PrepareCreateGenesisBlocks();            
            res = CreateShardGenesisBlocks(real_root_genesis_nodes,
                                           cons_genesis_nodes,
                                           shard_node_net_id,
                                           genesis_acount_balance_map); // root 节点账户创建在第一个 shard 网络
            assert(res == kInitSuccess);

            for (uint32_t i = 0; i < cons_genesis_nodes.size(); ++i) {
                prikeys.push_back(cons_genesis_nodes[i]->prikey);
            }

            ComputeG2sForNodes(prikeys);
        }
    }
    
    return res;
}

void GenesisBlockInit::ComputeG2sForNodes(const std::vector<std::string>& prikeys) {
    // 计算 bls 相关信息
    for (uint32_t k = 0; k < prikeys.size(); ++k) {
        std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
        secptr->SetPrivateKey(prikeys[k]);
        bls::protobuf::LocalPolynomial local_poly;
        std::vector<libff::alt_bn128_Fr> polynomial;
        prefix_db_->SaveLocalElectPos(secptr->GetAddress(), k);
        if (prefix_db_->GetLocalPolynomial(secptr, secptr->GetAddress(), &local_poly)) {
            for (int32_t i = 0; i < local_poly.polynomial_size(); ++i) {
                polynomial.push_back(libff::alt_bn128_Fr(
                    common::Encode::HexEncode(local_poly.polynomial(i)).c_str()));
            }

            uint32_t valid_n = prikeys.size();
            uint32_t valid_t = common::GetSignerCount(valid_n);
            libBLS::Dkg dkg_instance = libBLS::Dkg(valid_t, valid_n);
            auto contribution = dkg_instance.SecretKeyContribution(polynomial, valid_n, valid_t);
            uint32_t local_member_index_ = k;
            uint32_t change_idx = 0;
            auto new_g2 = polynomial[change_idx] * libff::alt_bn128_G2::one();
            auto old_g2 = polynomial[change_idx] * libff::alt_bn128_G2::one();
            for (uint32_t mem_idx = 0; mem_idx < valid_n; ++mem_idx) {
                if (mem_idx == local_member_index_) {
                    continue;
                }

                bls::protobuf::JoinElectBlsInfo verfy_final_vals;
                if (!prefix_db_->GetVerifiedG2s(
                        mem_idx,
                        secptr->GetAddress(),
                        valid_t,
                        &verfy_final_vals)) {
                    if (!CheckRecomputeG2s(mem_idx, valid_t, secptr->GetAddress(), verfy_final_vals)) {
                        assert(false);
                        continue;
                    }
                }

                bls::protobuf::JoinElectInfo join_info;
                if (!prefix_db_->GetNodeVerificationVector(secptr->GetAddress(), &join_info)) {
                    assert(false);
                    continue;
                }

                if (join_info.g2_req().verify_vec_size() <= (int32_t)change_idx) {
                    assert(false);
                    continue;
                }

                libff::alt_bn128_G2 old_val;
                {
                    auto& item = join_info.g2_req().verify_vec(change_idx);
                    auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
                    auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
                    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
                    auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
                    auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
                    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
                    auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
                    auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
                    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
                    old_val = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
                    assert(old_val == old_g2);
                }

                auto& item = verfy_final_vals.verified_g2();
                auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
                auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
                auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
                auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
                auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
                auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
                auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
                auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
                auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
                auto all_verified_val = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
                auto old_g2_val = power(libff::alt_bn128_Fr(mem_idx + 1), change_idx) * old_val;
                auto new_g2_val = power(libff::alt_bn128_Fr(mem_idx + 1), change_idx) * new_g2;
                assert(old_g2_val == new_g2_val);
                auto old_all = all_verified_val;
                all_verified_val = all_verified_val - old_g2_val + new_g2_val;
                assert(old_all == contribution[mem_idx] * libff::alt_bn128_G2::one());
                assert(all_verified_val == contribution[mem_idx] * libff::alt_bn128_G2::one());
            }
        }
    }

    db_->ClearPrefix("db_for_gid_");
}

void GenesisBlockInit::PrepareCreateGenesisBlocks() {
        std::shared_ptr<security::Security> security = nullptr;
        std::shared_ptr<sync::KeyValueSync> kv_sync = nullptr;
        // 初始化本节点所有的 tx pool 和 cross tx pool
        pools_mgr_ = std::make_shared<pools::TxPoolManager>(security, db_, kv_sync, nullptr);
        std::shared_ptr<pools::ShardStatistic> statistic_mgr = nullptr;
        std::shared_ptr<contract::ContractManager> ct_mgr = nullptr;
        account_mgr_->Init(1, db_, pools_mgr_);
        block_mgr_->Init(account_mgr_, db_, pools_mgr_, statistic_mgr, security, ct_mgr, "", nullptr, nullptr);
        return;
};


bool GenesisBlockInit::CheckRecomputeG2s(
        uint32_t local_member_index,
        uint32_t valid_t,
        const std::string& id,
        bls::protobuf::JoinElectBlsInfo& verfy_final_vals) {
    assert(valid_t > 1);
    bls::protobuf::JoinElectInfo join_info;
    if (!prefix_db_->GetNodeVerificationVector(id, &join_info)) {
        return false;
    }

    int32_t min_idx = 0;
    if (join_info.g2_req().verify_vec_size() >= 32) {
        min_idx = join_info.g2_req().verify_vec_size() - 32;
    }

    libff::alt_bn128_G2 verify_g2s = libff::alt_bn128_G2::zero();
    int32_t begin_idx = valid_t - 1;
    for (; begin_idx > min_idx; --begin_idx) {
        if (prefix_db_->GetVerifiedG2s(local_member_index, id, begin_idx + 1, &verfy_final_vals)) {
            auto& item = verfy_final_vals.verified_g2();
            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            verify_g2s = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
            ++begin_idx;
            break;
        }
    }

    if (verify_g2s == libff::alt_bn128_G2::zero()) {
        begin_idx = 0;
    }

    for (uint32_t i = begin_idx; i < valid_t; ++i) {
        auto& item = join_info.g2_req().verify_vec(i);
        auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
        auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
        auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
        auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
        auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
        auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
        auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
        auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
        auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
        auto g2 = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
        verify_g2s = verify_g2s + power(libff::alt_bn128_Fr(local_member_index + 1), i) * g2;
        bls::protobuf::VerifyVecItem& verify_item = *verfy_final_vals.mutable_verified_g2();
        verify_item.set_x_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.X.c0)));
        verify_item.set_x_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.X.c1)));
        verify_item.set_y_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Y.c0)));
        verify_item.set_y_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Y.c1)));
        verify_item.set_z_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Z.c0)));
        verify_item.set_z_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Z.c1)));
        auto verified_val = verfy_final_vals.SerializeAsString();
        prefix_db_->SaveVerifiedG2s(local_member_index, id, i + 1, verfy_final_vals);
        ZJC_DEBUG("success save verified g2: %u, peer: %d, t: %d, %s, %s",
            local_member_index,
            join_info.member_idx(),
            i + 1,
            common::Encode::HexEncode(id).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.X.c0).c_str());
    }

    return true;
}

bool GenesisBlockInit::CreateNodePrivateInfo(
        uint32_t sharding_id,
        uint64_t elect_height,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes) {
    static const uint32_t n = common::GlobalInfo::Instance()->each_shard_max_members();
    static const uint32_t t = common::GetSignerCount(n);
    libBLS::Dkg dkg_instance = libBLS::Dkg(t, n);
    uint32_t valid_n = genesis_nodes.size();
    uint32_t valid_t = common::GetSignerCount(valid_n);
    std::vector<std::vector<libff::alt_bn128_Fr>> secret_key_contribution(valid_n);
    for (uint32_t idx = 0; idx < genesis_nodes.size(); ++idx) {
        std::string file = std::string("./") + common::Encode::HexEncode(genesis_nodes[idx]->id);
        bool file_valid = true;
        bls::protobuf::LocalPolynomial local_poly;
        FILE* fd = fopen(file.c_str(), "r");
        if (fd != nullptr) {
            char* data = new char[1024 * 1024 * 10];
            if (fgets(data, 1024 * 1024 * 10, fd) == nullptr) {
                ZJC_FATAL("load bls init info failed: %s", file.c_str());
                return false;
            }

            fclose(fd);
            std::string tmp_data(data, strlen(data) - 1);
            std::string val = common::Encode::HexDecode(tmp_data);
            if (!local_poly.ParseFromString(val)) {
                ZJC_FATAL("load bls init info failed!");
                return false;
            }
        }

        if (local_poly.polynomial_size() <= 0) {
            // just private key.
            genesis_nodes[idx]->polynomial = dkg_instance.GeneratePolynomial();
            for (uint32_t j = 0; j < genesis_nodes[idx]->polynomial.size(); ++j) {
                local_poly.add_polynomial(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(genesis_nodes[idx]->polynomial[j])));
            }

            FILE* fd = fopen(file.c_str(), "w");
            std::string val = common::Encode::HexEncode(local_poly.SerializeAsString()) + "\n";
            fputs(val.c_str(), fd);
            fclose(fd);
        } else {
            for (int32_t poly_idx = 0; poly_idx < local_poly.polynomial_size(); ++poly_idx) {
                genesis_nodes[idx]->polynomial.push_back(
                    libff::alt_bn128_Fr(common::Encode::HexEncode(local_poly.polynomial(poly_idx)).c_str()));
            }
        }

        std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
        secptr->SetPrivateKey(genesis_nodes[idx]->prikey);
        genesis_nodes[idx]->verification = dkg_instance.VerificationVector(genesis_nodes[idx]->polynomial);
        secret_key_contribution[idx] = dkg_instance.SecretKeyContribution(
            genesis_nodes[idx]->polynomial, valid_n, valid_t);
        bls::protobuf::JoinElectInfo join_info;
        join_info.set_member_idx(idx);
        join_info.set_shard_id(sharding_id);
        auto* req = join_info.mutable_g2_req();
        auto g2_vec = dkg_instance.VerificationVector(genesis_nodes[idx]->polynomial);
        std::string str_for_hash;
        str_for_hash.append((char*)&sharding_id, sizeof(sharding_id));
        str_for_hash.append((char*)&idx, sizeof(idx));
        for (uint32_t i = 0; i < t; ++i) {
            bls::protobuf::VerifyVecItem& verify_item = *req->add_verify_vec();
            verify_item.set_x_c0(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].X.c0)));
            verify_item.set_x_c1(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].X.c1)));
            verify_item.set_y_c0(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Y.c0)));
            verify_item.set_y_c1(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Y.c1)));
            verify_item.set_z_c0(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Z.c0)));
            verify_item.set_z_c1(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Z.c1)));
            str_for_hash.append(verify_item.x_c0());
            str_for_hash.append(verify_item.x_c1());
            str_for_hash.append(verify_item.y_c0());
            str_for_hash.append(verify_item.y_c1());
            str_for_hash.append(verify_item.z_c0());
            str_for_hash.append(verify_item.z_c1());
        }

        genesis_nodes[idx]->check_hash = common::Hash::keccak256(str_for_hash);
        prefix_db_->SaveLocalPolynomial(secptr, secptr->GetAddress(), local_poly);
        prefix_db_->AddBlsVerifyG2(secptr->GetAddress(), *req);
        prefix_db_->SaveTemporaryKv(genesis_nodes[idx]->check_hash, join_info.SerializeAsString());
    }

    for (size_t i = 0; i < genesis_nodes.size(); ++i) {
        for (size_t j = i; j < genesis_nodes.size(); ++j) {
            std::swap(secret_key_contribution[j][i], secret_key_contribution[i][j]);
        }
    }

    libBLS::Dkg tmpdkg(valid_t, valid_n);
    auto common_public_key = libff::alt_bn128_G2::zero();
    for (uint32_t idx = 0; idx < genesis_nodes.size(); ++idx) {
        std::vector<libff::alt_bn128_Fr> tmp_secret_key_contribution;
        for (uint32_t i = 0; i < genesis_nodes.size(); ++i) {
            tmp_secret_key_contribution.push_back(secret_key_contribution[idx][i]);
        }

        genesis_nodes[idx]->bls_prikey = tmpdkg.SecretKeyShareCreate(tmp_secret_key_contribution);
        genesis_nodes[idx]->bls_pubkey = tmpdkg.GetPublicKeyFromSecretKey(genesis_nodes[idx]->bls_prikey);
        common_public_key = common_public_key + genesis_nodes[idx]->verification[0];
        std::string enc_data;
        security::Ecdsa ecdsa;
        if (ecdsa.Encrypt(
                libBLS::ThresholdUtils::fieldElementToString(genesis_nodes[idx]->bls_prikey),
                genesis_nodes[idx]->prikey,
                &enc_data) != security::kSecuritySuccess) {
            ZJC_FATAL("encrypt data failed!");
            return false;
        }

        prefix_db_->SaveBlsPrikey(
            elect_height,
            sharding_id,
            genesis_nodes[idx]->id,
            enc_data);
    }

    common_pk_[sharding_id] = common_public_key;
    return true;
}

// GetGenesisAccountBalanceMap 计算每个节点要分配的余额
std::unordered_map<std::string, uint64_t> GenesisBlockInit::GetGenesisAccountBalanceMap(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards) {
    // 去个重
    std::set<std::string> valid_ids;
    for (auto iter = root_genesis_nodes.begin(); iter != root_genesis_nodes.end(); ++iter) {
        if (valid_ids.find((*iter)->id) != valid_ids.end()) {
            continue;
        }

        valid_ids.insert((*iter)->id);
    }

    for (auto cons_genesis_nodes : cons_genesis_nodes_of_shards) {
        for (auto iter = cons_genesis_nodes.begin(); iter != cons_genesis_nodes.end(); ++iter) {
            if (valid_ids.find((*iter)->id) != valid_ids.end()) {
                continue;
            }

            valid_ids.insert((*iter)->id);
        }
    }

    uint64_t aver_balance = common::kGenesisShardingNodesMaxZjc / valid_ids.size();
    uint64_t rest_balance = common::kGenesisShardingNodesMaxZjc % valid_ids.size();
    
    std::unordered_map<std::string, uint64_t> node_balance_map;
    // 平均分配余额，剩下的都给最后一个
    uint32_t count;
    for (auto it = valid_ids.begin(); it != valid_ids.end(); ++it, ++count) {
        uint64_t balance = aver_balance;
        if (count == valid_ids.size() - 1) {
            balance += rest_balance;
        }
        node_balance_map.insert(std::pair<std::string, uint64_t>(*it, balance));
    }

    return node_balance_map;
}

void GenesisBlockInit::SetPrevElectInfo(
        const elect::protobuf::ElectBlock& elect_block,
        block::protobuf::BlockTx& block_tx) {
    block::protobuf::Block block_item;
    auto res = prefix_db_->GetBlockWithHeight(
        network::kRootCongressNetworkId,
        elect_block.shard_network_id() % common::kImmutablePoolSize,
        elect_block.prev_members().prev_elect_height(),
        &block_item);
    if (!res) {
        ELECT_ERROR("get prev block error[%d][%d][%lu].",
            network::kRootCongressNetworkId,
            common::kRootChainPoolIndex,
            elect_block.prev_members().prev_elect_height());
        return;
    }

    if (block_item.tx_list_size() != 1) {
        ELECT_ERROR("not has tx list size.");
        assert(false);
        return;
    }

    elect::protobuf::ElectBlock prev_elect_block;
    bool ec_block_loaded = false;
    for (int32_t i = 0; i < block_item.tx_list(0).storages_size(); ++i) {
        if (block_item.tx_list(0).storages(i).key() == protos::kElectNodeAttrElectBlock) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(block_item.tx_list(0).storages(i).val_hash(), &val)) {
                ZJC_ERROR("elect block get temp kv from db failed!");
                return;
            }

            prev_elect_block.ParseFromString(val);
            ec_block_loaded = true;
            break;
        }
    }

    if (!ec_block_loaded) {
        assert(false);
        return;
    }

    auto kv = block_tx.add_storages();
    kv->set_key(protos::kShardElectionPrevInfo);
    std::string val_hash = protos::GetElectBlockHash(prev_elect_block);
    kv->set_val_hash(val_hash);
    prefix_db_->SaveTemporaryKv(val_hash, prev_elect_block.SerializeAsString());
    return;
}

int GenesisBlockInit::CreateElectBlock(
        uint32_t shard_netid, // 要被选举的 shard 网络
        std::string& root_pre_hash,
        uint64_t height,
        uint64_t prev_height,
        FILE* root_gens_init_block_file,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes) {
    if (genesis_nodes.size() == 0) {
        return kInitSuccess;
    }
    
    // ??? 不应该是 pool_index 吗，为什么是 shard_netid
    auto account_info = account_mgr_->pools_address_info(shard_netid);
    auto tenon_block = std::make_shared<block::protobuf::Block>();
    auto tx_list = tenon_block->mutable_tx_list();
    auto tx_info = tx_list->Add();
    tx_info->set_step(pools::protobuf::kConsensusRootElectShard);
    tx_info->set_from("");
    tx_info->set_to(account_info->addr());
    tx_info->set_amount(0);
    tx_info->set_gas_limit(0);
    tx_info->set_gas_used(0);
    tx_info->set_balance(0);
    tx_info->set_status(0);
    elect::protobuf::ElectBlock ec_block;
    // 期望 leader 数量
    uint32_t expect_leader_count = (uint32_t)pow(
        2.0,
        (double)((uint32_t)log2(double(genesis_nodes.size() / 3))));
    
    uint32_t node_idx = 0;
    for (auto iter = genesis_nodes.begin(); iter != genesis_nodes.end(); ++iter, ++node_idx) {
        auto in = ec_block.add_in();
        in->set_pubkey((*iter)->pubkey);
        in->set_pool_idx_mod_num(node_idx < expect_leader_count ? node_idx : -1);
    }

    tenon_block->set_height(height);
    ec_block.set_shard_network_id(shard_netid);
    ec_block.set_elect_height(tenon_block->height());
    
    if (prev_height != common::kInvalidUint64) {
        auto prev_members = ec_block.mutable_prev_members();
        for (uint32_t i = 0; i < genesis_nodes.size(); ++i) {
            auto mem_pk = prev_members->add_bls_pubkey();
            auto tmp_pk = std::make_shared<BLSPublicKey>(genesis_nodes[i]->bls_pubkey);
            auto pkeys_str = tmp_pk->toString();
            mem_pk->set_x_c0(pkeys_str->at(0));
            mem_pk->set_x_c1(pkeys_str->at(1));
            mem_pk->set_y_c0(pkeys_str->at(2));
            mem_pk->set_y_c1(pkeys_str->at(3));
            mem_pk->set_pool_idx_mod_num(i < expect_leader_count ? i : -1);
        }

        auto common_pk_ptr = std::make_shared<BLSPublicKey>(common_pk_[shard_netid]);
        auto common_pk_strs = common_pk_ptr->toString();
        auto common_pk = prev_members->mutable_common_pubkey();
        common_pk->set_x_c0(common_pk_strs->at(0));
        common_pk->set_x_c1(common_pk_strs->at(1));
        common_pk->set_y_c0(common_pk_strs->at(2));
        common_pk->set_y_c1(common_pk_strs->at(3));
        prev_members->set_prev_elect_height(prev_height);
        db::DbWriteBatch db_batch;
        prefix_db_->SaveElectHeightCommonPk(shard_netid, prev_height, *prev_members, db_batch);
        auto st = db_->Put(db_batch);
        assert(st.ok());
        SetPrevElectInfo(ec_block, *tx_info);
    }

    auto storage = tx_info->add_storages();
    storage->set_key(protos::kElectNodeAttrElectBlock);
    std::string val = ec_block.SerializeAsString();
    std::string val_hash = protos::GetElectBlockHash(ec_block);
    storage->set_val_hash(val_hash);
    prefix_db_->SaveTemporaryKv(val_hash, val);
    tenon_block->set_prehash(root_pre_hash);
    tenon_block->set_version(common::kTransactionVersion);
    // 这个 pool index 用了 shard 的值而已
    tenon_block->set_pool_index(shard_netid);
    tenon_block->set_network_id(network::kRootCongressNetworkId);
    tenon_block->set_is_commited_block(true);
    tenon_block->set_electblock_height(1);
    tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
    BlsAggSignBlock(root_genesis_nodes, tenon_block);
    db::DbWriteBatch db_batch;
    pools_mgr_->UpdateLatestInfo(
        0,
        tenon_block->network_id(),
        tenon_block->pool_index(),
        tenon_block->height(),
        tenon_block->hash(),
        tenon_block->prehash(),
        db_batch);

    prefix_db_->SaveLatestElectBlock(ec_block, db_batch);
    ZJC_DEBUG("success save latest elect block: %u, %lu", ec_block.shard_network_id(), ec_block.elect_height());
    std::string ec_val = common::Encode::HexEncode(tenon_block->SerializeAsString()) +
        "-" + common::Encode::HexEncode(ec_block.SerializeAsString()) + "\n";
    fputs(ec_val.c_str(), root_gens_init_block_file);
    AddBlockItemToCache(tenon_block, db_batch);
    block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block, db_batch);
    block_mgr_->GenesisNewBlock(0, tenon_block);
    db_->Put(db_batch);

    auto account_ptr = account_mgr_->GetAcountInfoFromDb(account_info->addr());
    if (account_ptr == nullptr) {
        ZJC_FATAL("get address failed! [%s]",
            common::Encode::HexEncode(account_info->addr()).c_str());
        return kInitError;
    }

    if (account_ptr->balance() != 0) {
        ZJC_FATAL("get address balance failed! [%s]",
            common::Encode::HexEncode(account_info->addr()).c_str());
        return kInitError;
    }

    uint64_t elect_height = 0;
    std::string elect_block_str;

    root_pre_hash = consensus::GetBlockHash(*tenon_block);
    return kInitSuccess;
}

int GenesisBlockInit::GenerateRootSingleBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        FILE* root_gens_init_block_file,
        uint64_t* root_pool_height) {
    if (root_gens_init_block_file == nullptr) {
        return kInitError;
    }

    GenerateRootAccounts();
    uint64_t root_single_block_height = 0llu;
    // for root single block chain
    // 呃，这个账户不是已经创建了么
    auto root_pool_addr = common::kRootPoolsAddress;
    std::string root_pre_hash;
    {
        // 创建一个块用于创建root_pool_addr账户
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_gid(common::CreateGID(""));
        tx_info->set_from("");
        tx_info->set_to(root_pool_addr);
        tx_info->set_amount(0);
        tx_info->set_balance(0);
        tx_info->set_gas_limit(0);
        tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        tenon_block->set_prehash("");
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(common::kRootChainPoolIndex); // pool_index 为 256
        tenon_block->set_height(root_single_block_height++);
        tenon_block->set_electblock_height(1);
        // TODO 此处 network_id 一定是 root
        tenon_block->set_network_id(common::GlobalInfo::Instance()->network_id());
        tenon_block->set_is_commited_block(true);
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        
        BlsAggSignBlock(genesis_nodes, tenon_block);
        fputs((common::Encode::HexEncode(tenon_block->SerializeAsString()) + "\n").c_str(),
            root_gens_init_block_file);
        db::DbWriteBatch db_batch;
        pools_mgr_->UpdateLatestInfo(
            0,
            tenon_block->network_id(),
            tenon_block->pool_index(),
            tenon_block->height(),
            tenon_block->hash(),
            tenon_block->prehash(),
            db_batch);

        AddBlockItemToCache(tenon_block, db_batch);
        block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block, db_batch);
        block_mgr_->GenesisNewBlock(0, tenon_block);
        db_->Put(db_batch);
        std::string pool_hash;
        uint64_t pool_height = 0;
        uint64_t tm_height;
        uint64_t tm_with_block_height;

        root_pre_hash = consensus::GetBlockHash(*tenon_block);
    }

    {
        // 创建时间块
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_gid(common::CreateGID(""));
        tx_info->set_from("");
        tx_info->set_to(root_pool_addr);
        tx_info->set_amount(0);
        tx_info->set_balance(0);
        tx_info->set_gas_limit(0);
        tx_info->set_step(pools::protobuf::kConsensusRootTimeBlock);
        tx_info->set_gas_limit(0llu);
        tx_info->set_amount(0);
        
        auto timeblock_storage = tx_info->add_storages();
        tenon_block->set_height(root_single_block_height++);
        timeblock::protobuf::TimeBlock tm_block;
        tm_block.set_timestamp(common::TimeUtils::TimestampSeconds());
        tm_block.set_height(tenon_block->height());
        tm_block.set_vss_random(common::Random::RandomUint64());
        timeblock_storage->set_key(protos::kAttrTimerBlock);
        
        char data[16];
        uint64_t* u64_data = (uint64_t*)data;
        u64_data[0] = tm_block.timestamp();
        u64_data[1] = tm_block.vss_random();
        timeblock_storage->set_val_hash(std::string(data, sizeof(data)));
        auto genesis_tmblock = tx_info->add_storages();
        genesis_tmblock->set_key(protos::kAttrGenesisTimerBlock);
//         auto vss_random_attr = tx_info->add_attr();
//         vss_random_attr->set_key(tmblock::kVssRandomAttr);
//         vss_random_attr->set_value(std::to_string(now_tm));
        tenon_block->set_prehash(root_pre_hash);
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(common::kRootChainPoolIndex);
        tenon_block->set_electblock_height(1);
        // TODO network_id 一定是 root
        tenon_block->set_network_id(common::GlobalInfo::Instance()->network_id());
        tenon_block->set_is_commited_block(true);
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        
        BlsAggSignBlock(genesis_nodes, tenon_block);
        auto tmp_str = tenon_block->SerializeAsString();
        block::protobuf::Block tenon_block2;
        tenon_block2.ParseFromString(tmp_str);
        assert(tenon_block2.tx_list_size() > 0);
        db::DbWriteBatch db_batch;
        prefix_db_->SaveGenesisTimeblock(tm_block.height(), tm_block.timestamp(), db_batch);
        pools_mgr_->UpdateLatestInfo(
            0,
            tenon_block->network_id(),
            tenon_block->pool_index(),
            tenon_block->height(),
            tenon_block->hash(),
            tenon_block->prehash(),
            db_batch);

        prefix_db_->SaveLatestTimeBlock(tenon_block->height(), db_batch);
        prefix_db_->SaveConsensusedStatisticTimeBlockHeight(
            network::kRootCongressNetworkId, tenon_block->height(), db_batch);
        fputs((common::Encode::HexEncode(tmp_str) + "\n").c_str(), root_gens_init_block_file);
//         tmblock::TimeBlockManager::Instance()->UpdateTimeBlock(1, now_tm, now_tm);
        AddBlockItemToCache(tenon_block, db_batch);
        block_mgr_->GenesisNewBlock(0, tenon_block);
        block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block, db_batch);
        db_->Put(db_batch);
        std::string pool_hash;
        uint64_t pool_height = 0;
        uint64_t tm_height;
        uint64_t tm_with_block_height;

        root_pre_hash = consensus::GetBlockHash(*tenon_block);
    }

    *root_pool_height = root_single_block_height - 1;
    return kInitSuccess;
}

int GenesisBlockInit::GenerateShardSingleBlock(uint32_t sharding_id) {
    FILE* root_gens_init_block_file = fopen("./root_blocks", "r");
    if (root_gens_init_block_file == nullptr) {
        return kInitError;
    }

    char data[20480];
    uint32_t block_count = 0;
    db::DbWriteBatch db_batch;
    while (fgets(data, 20480, root_gens_init_block_file) != nullptr) {
        // root_gens_init_block_file 中保存的是 root pool 账户 block，和时间快 block，同步过来
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        std::string tmp_data(data, strlen(data) - 1);
        common::Split<> tmp_split(tmp_data.c_str(), '-', tmp_data.size());
        std::string block_str = tmp_data;
        std::string ec_block_str;
        if (tmp_split.Count() == 2) {
            block_str = tmp_split[0];
            ec_block_str = common::Encode::HexDecode(tmp_split[1]);
        }

        if (!tenon_block->ParseFromString(common::Encode::HexDecode(block_str))) {
            assert(false);
            return kInitError;
        }

        AddBlockItemToCache(tenon_block, db_batch);
        // 同步 root_gens_init_block_file 中 block 中的账户和 block
        // 无非就是各节点账户，上文中已经加过了，这里不好区分 root_blocks 不同 shard 的账户
        // block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block, db_batch);

        // 选举块、时间块无论 shard 都是要全网同步的
        block_mgr_->GenesisNewBlock(0, tenon_block);
        for (int32_t i = 0; i < tenon_block->tx_list_size(); ++i) {
            for (int32_t j = 0; j < tenon_block->tx_list(i).storages_size(); ++j) {
                if (tenon_block->tx_list(i).storages(j).key() == protos::kElectNodeAttrElectBlock) {
                    elect::protobuf::ElectBlock ec_block;
                    if (!ec_block.ParseFromString(ec_block_str)) {
                        assert(false);
                    }

                    std::string val_hash = protos::GetElectBlockHash(ec_block);
                    // 同步选举块
                    prefix_db_->SaveTemporaryKv(val_hash, ec_block_str);
                    prefix_db_->SaveLatestElectBlock(ec_block, db_batch);
                    ZJC_DEBUG("save elect block sharding: %u, height: %u, has prev: %d, has common_pk: %d",
                        ec_block.shard_network_id(),
                        ec_block.elect_height(),
                        ec_block.has_prev_members(),
                        ec_block.prev_members().has_common_pubkey());
                }
                // 同步时间块
                if (tenon_block->tx_list(i).storages(j).key() == protos::kAttrGenesisTimerBlock) {
                    prefix_db_->SaveGenesisTimeblock(tenon_block->height(), tenon_block->timestamp(), db_batch);
                }

                if (tenon_block->tx_list(i).storages(j).key() == protos::kAttrTimerBlock) {
                    prefix_db_->SaveLatestTimeBlock(tenon_block->height(), db_batch);
                    prefix_db_->SaveConsensusedStatisticTimeBlockHeight(
                        sharding_id, tenon_block->height(), db_batch);
                }
            }
        }
    }
    // flush 磁盘
    db_->Put(db_batch);
    {
        auto addr_info = account_mgr_->pools_address_info(common::kRootChainPoolIndex);
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(addr_info->addr());
        if (account_ptr == nullptr) {
            ZJC_FATAL("get address info failed! [%s]",
                common::Encode::HexEncode(addr_info->addr()).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != 0) {
            ZJC_FATAL("get address balance failed! [%s]",
                common::Encode::HexEncode(addr_info->addr()).c_str());
            return kInitError;
        }
    }

    return kInitSuccess;
}

std::string GenesisBlockInit::GetValidPoolBaseAddr(uint32_t pool_index) {
    if (pool_index == common::kRootChainPoolIndex) {
        return common::kRootPoolsAddress;
    }

    return account_mgr_->pools_address_info(pool_index)->addr();
}

// CreateRootGenesisBlocks 为 root 网络的每个 pool 生成创世块
int GenesisBlockInit::CreateRootGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map) {
    // 256 个 root 创世账号
    GenerateRootAccounts();
    // 256 x shard_num 个 shard 创世账号
    InitShardGenesisAccount();
    uint64_t genesis_account_balance = 0llu;
    uint64_t all_balance = 0llu;
    pools::protobuf::StatisticTxItem init_heights;
    std::unordered_map<uint32_t, std::string> pool_prev_hash_map;
    std::string prehashes[common::kImmutablePoolSize]; // 256
    
    // 为创世账户在 root 网络中创建创世块
    // 创世块中包含：创建初始账户，以及节点选举类型的交易
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        // 用于聚合不同 net_id 的交易，供创建账户使用
        std::map<block::protobuf::BlockTx*, uint32_t> tx2net_map_for_account; 
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        auto iter = root_account_with_pool_index_map_.find(i);
        std::string address = iter->second;
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            // 每个 root 账户地址都对应一个 pool 账户，先把创世账户中涉及到的 pool 账户创建出来
            tx_info->set_to(GetValidPoolBaseAddr(common::GetAddressPoolIndex(address)));
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
            // root 网络的 pool addr 账户创建在 shard3?
            tx2net_map_for_account.insert(std::make_pair(tx_info, network::kConsensusShardBeginNetworkId));
        }
        // TODO 一样，可以试试删掉这个
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(GetValidPoolBaseAddr(common::GetAddressPoolIndex(address)));
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
            tx2net_map_for_account.insert(std::make_pair(tx_info, network::kConsensusShardBeginNetworkId));
        }

        // 创建 root 创世账户，貌似没什么用
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(address);
            tx_info->set_amount(genesis_account_balance); // 余额 0 即可
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
            // root 创世账户也创建在 shard3?
            tx2net_map_for_account.insert(std::make_pair(tx_info, network::kConsensusShardBeginNetworkId));
        }

        for (auto shard_iter = net_pool_index_map_.begin(); shard_iter != net_pool_index_map_.end(); ++shard_iter) {
            std::map<uint32_t, std::string> pool_map = shard_iter->second;
            uint32_t net_id = shard_iter->first;
            auto pool_iter = pool_map.find(i);
            if (pool_iter != pool_map.end()) {
                std::string shard_acc_address = pool_iter->second;
                // 向 shard 账户转账，root 网络中的账户余额不重要，主要是记录下此 block 的 shard 信息即可
                auto tx_info = tx_list->Add();
                tx_info->set_gid(common::CreateGID(""));
                tx_info->set_from("");
                tx_info->set_to(shard_acc_address);
                tx_info->set_amount(genesis_account_balance);
                tx_info->set_balance(genesis_account_balance);
                tx_info->set_gas_limit(0);
                tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
                // shard 创世账户创建在对应的 net
                tx2net_map_for_account.insert(std::make_pair(tx_info, net_id));
            }
        }


        for (uint32_t member_idx = 0; member_idx < root_genesis_nodes.size(); ++member_idx) {
            // 将 root 节点的选举交易打包到对应的 pool 块中
            if (common::GetAddressPoolIndex(root_genesis_nodes[member_idx]->id) == i) {
                auto join_elect_tx_info = tx_list->Add();
                join_elect_tx_info->set_step(pools::protobuf::kJoinElect);
                join_elect_tx_info->set_from(root_genesis_nodes[member_idx]->id);
                join_elect_tx_info->set_to("");
                join_elect_tx_info->set_amount(0);
                join_elect_tx_info->set_gas_limit(0);
                join_elect_tx_info->set_gas_used(0);
                join_elect_tx_info->set_balance(0);
                join_elect_tx_info->set_status(0);
                auto storage = join_elect_tx_info->add_storages();
                storage->set_key(protos::kJoinElectVerifyG2);
                storage->set_val_hash(root_genesis_nodes[member_idx]->check_hash);
                storage->set_val_size(33);
                // root 节点选举涉及账户分配到 shard3 网络
                tx2net_map_for_account.insert(std::make_pair(join_elect_tx_info, network::kConsensusShardBeginNetworkId));
            }
        }

        for (uint32_t k = 0; k < cons_genesis_nodes_of_shards.size(); k++) {
            uint32_t net_id = k + network::kConsensusShardBeginNetworkId;
            auto cons_genesis_nodes = cons_genesis_nodes_of_shards[k];
            
            for (uint32_t member_idx = 0; member_idx < cons_genesis_nodes.size(); ++member_idx) {
                // 同理，shard 节点的选举交易也打包到对应的 pool 块中
                if (common::GetAddressPoolIndex(cons_genesis_nodes[member_idx]->id) == i) {
                    auto join_elect_tx_info = tx_list->Add();
                    join_elect_tx_info->set_step(pools::protobuf::kJoinElect);
                    join_elect_tx_info->set_from(cons_genesis_nodes[member_idx]->id);
                    join_elect_tx_info->set_to("");
                    join_elect_tx_info->set_amount(0);
                    join_elect_tx_info->set_gas_limit(0);
                    join_elect_tx_info->set_gas_used(0);
                    join_elect_tx_info->set_balance(0);
                    join_elect_tx_info->set_status(0);
                    auto storage = join_elect_tx_info->add_storages();
                    storage->set_key(protos::kJoinElectVerifyG2);
                    storage->set_val_hash(cons_genesis_nodes[member_idx]->check_hash);
                    storage->set_val_size(33);
                    // 选举交易涉及账户分配到对应 shard
                    tx2net_map_for_account.insert(std::make_pair(join_elect_tx_info, net_id));
                }
            }
        }

        tenon_block->set_prehash("");
        tenon_block->set_version(common::kTransactionVersion);
        // 为此 shard 的此 pool 打包一个块，这个块中有某些创世账户的生成交易，有某些root和shard节点的选举交易
        tenon_block->set_pool_index(iter->first);
        tenon_block->set_height(0);
        tenon_block->set_timeblock_height(0);
        tenon_block->set_electblock_height(1);
        // 块所属的 network 自然是要创建的网络，这个函数是 root 网络，network_id 自然是 root
        tenon_block->set_network_id(network::kRootCongressNetworkId);
        tenon_block->set_is_commited_block(true);
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        // 所有 root 节点对块进行签名
        BlsAggSignBlock(root_genesis_nodes, tenon_block);
        prehashes[i] = tenon_block->hash();
        // 更新对应 pool 当前最新块的 hash 值
        pool_prev_hash_map[iter->first] = tenon_block->hash();
        db::DbWriteBatch db_batch;

        // 更新交易池最新信息
        pools_mgr_->UpdateLatestInfo(
            0,
            tenon_block->network_id(),
            tenon_block->pool_index(),
            tenon_block->height(),
            tenon_block->hash(),
            tenon_block->prehash(),
            db_batch);
        // ??? 和 UpdateLatestInfo 差不多啊，冗余了吧
        AddBlockItemToCache(tenon_block, db_batch);

        // 持久化块中涉及的庄户信息，统一创建块当中的账户们到 shard 3
        // 包括 root 创世账户，shard 创世账户，root 和 shard 节点账户

        // 这里将 block 中涉及的账户信息，在不同的 network 中创建
        // 其实和 CreateShartGenesisBlocks 中对于 shard 创世账户的持久化部分重复了，但由于是 kv 所以没有影响
        for (auto it = tx2net_map_for_account.begin(); it != tx2net_map_for_account.end(); ++it) {
            auto tx = it->first;
            uint32_t net_id = it->second;
            
            block_mgr_->GenesisAddOneAccount(net_id, *tx, tenon_block->height(), db_batch);
        }
        // 出块，并处理块中不同类型的交易
        block_mgr_->GenesisNewBlock(0, tenon_block);
        // 处理选举交易（??? 这里没有和 GenesisNewBlock 重复吗）
        // TODO 感觉重复，可实验
        for (uint32_t i = 0; i < root_genesis_nodes.size(); ++i) {
            for (int32_t tx_idx = 0; tx_idx < tenon_block->tx_list_size(); ++tx_idx) {
                if (tenon_block->tx_list(tx_idx).step() == pools::protobuf::kJoinElect) {
                    block_mgr_->HandleJoinElectTx(0, *tenon_block, tenon_block->tx_list(tx_idx), db_batch);
                }
            }
        }

        db_->Put(db_batch);
        init_heights.add_heights(0);

        // 获取该 pool 对应的 root 账户，做一些余额校验，这里 root 账户中余额其实是 0
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            ZJC_FATAL("get address info failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != genesis_account_balance) {
            ZJC_FATAL("get address balance failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        all_balance += account_ptr->balance();
    }

    // 选举 root leader，选举 shard leader
    // 每次 ElectBlock 出块会生效前一个选举块
    FILE* root_gens_init_block_file = fopen("./root_blocks", "w");
    if (CreateElectBlock(
            network::kRootCongressNetworkId,
            prehashes[network::kRootCongressNetworkId],
            1,
            common::kInvalidUint64,
            root_gens_init_block_file,
            root_genesis_nodes,
            root_genesis_nodes) != kInitSuccess) {
        ZJC_FATAL("CreateElectBlock kRootCongressNetworkId failed!");
        return kInitError;
    }

    if (CreateElectBlock(
            network::kRootCongressNetworkId,
            prehashes[network::kRootCongressNetworkId],
            2,
            1,
            root_gens_init_block_file,
            root_genesis_nodes,
            root_genesis_nodes) != kInitSuccess) {
        ZJC_FATAL("CreateElectBlock kRootCongressNetworkId failed!");
        return kInitError;
    }

    // 这也应该是 pool_index，其实就是选了 root network 的 pool 2 和 pool 3 ?
    pool_prev_hash_map[network::kRootCongressNetworkId] = prehashes[network::kRootCongressNetworkId];
    init_heights.set_heights(network::kRootCongressNetworkId, 2);

    // prehashes 不是 pool 当中前一个块的 hash 吗，为什么是 prehashes[network_id] 而不是 prehashes[pool_index]
    for (uint32_t i = 0; i < cons_genesis_nodes_of_shards.size(); i++) {
        uint32_t net_id = i + network::kConsensusShardBeginNetworkId;
        GenisisNodeInfoPtrVector genesis_nodes = cons_genesis_nodes_of_shards[i];
        if (CreateElectBlock(
                net_id,
                prehashes[net_id],
                1,
                common::kInvalidUint64,
                root_gens_init_block_file,
                root_genesis_nodes,
                genesis_nodes) != kInitSuccess) {
            ZJC_FATAL("CreateElectBlock kConsensusShardBeginNetworkId failed!");
            return kInitError;
        }

        if (CreateElectBlock(
                net_id,
                prehashes[net_id],
                2,
                1,
                root_gens_init_block_file,
                root_genesis_nodes,
                genesis_nodes) != kInitSuccess) {
            ZJC_FATAL("CreateElectBlock kConsensusShardBeginNetworkId failed!");
            return kInitError;
        }

        pool_prev_hash_map[net_id] = prehashes[net_id];
        init_heights.set_heights(net_id, 2);
    }
    
    if (all_balance != 0) {
        ZJC_FATAL("balance all error[%llu][%llu]", all_balance, common::kGenesisFoundationMaxZjc);
        return kInitError;
    }

    uint64_t root_pool_height = 0;
    // pool256 中创建时间块 
    int res = GenerateRootSingleBlock(root_genesis_nodes, root_gens_init_block_file, &root_pool_height);
    if (res == kInitSuccess) {
        init_heights.add_heights(root_pool_height);
        
        std::vector<GenisisNodeInfoPtr> all_cons_genesis_nodes;
        for (std::vector<GenisisNodeInfoPtr> nodes : cons_genesis_nodes_of_shards) {
            all_cons_genesis_nodes.insert(all_cons_genesis_nodes.end(), nodes.begin(), nodes.end());
        }

        // 在 root 网络中为所有节点创建块
        CreateShardNodesBlocks(
            pool_prev_hash_map,
            root_genesis_nodes,
            all_cons_genesis_nodes,
            network::kRootCongressNetworkId,
            init_heights,
            genesis_acount_balance_map);
        prefix_db_->SaveStatisticLatestHeihgts(network::kRootCongressNetworkId, init_heights);
        std::string init_consensus_height;
        for (int32_t i = 0; i < init_heights.heights_size(); ++i) {
            init_consensus_height += std::to_string(init_heights.heights(i)) + " ";
        }

        ZJC_DEBUG("0 success change min elect statistic heights: %u, %s",
            network::kRootCongressNetworkId, init_consensus_height.c_str());
    }

    fclose(root_gens_init_block_file);
    return res;
}

bool GenesisBlockInit::BlsAggSignBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        std::shared_ptr<block::protobuf::Block>& block) try {
    std::vector<libff::alt_bn128_G1> all_signs;
    uint32_t n = genesis_nodes.size();
    uint32_t t = common::GetSignerCount(n);
    std::vector<size_t> idx_vec;
    auto g1_hash = libBLS::Bls::Hashing(block->hash());
    for (uint32_t i = 0; i < t; ++i) {
        libff::alt_bn128_G1 sign;
        bls::BlsSign::Sign(
            t,
            n,
            genesis_nodes[i]->bls_prikey,
            g1_hash,
            &sign);
        all_signs.push_back(sign);
        idx_vec.push_back(i + 1);
    }

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
    libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
    auto agg_sign = std::make_shared<libff::alt_bn128_G1>(
        bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
    if (!libBLS::Bls::Verification(g1_hash, *agg_sign, common_pk_[block->network_id()])) {
        ZJC_FATAL("agg sign failed shard: %u", block->network_id());
        return false;
    }

    agg_sign->to_affine_coordinates();
    block->set_bls_agg_sign_x(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(agg_sign->X)));
    block->set_bls_agg_sign_y(
        common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(agg_sign->Y)));
    ZJC_DEBUG("verification agg sign success hash: %s, signx: %s, common pk x: %s",
        common::Encode::HexEncode(block->hash()).c_str(),
        common::Encode::HexEncode(block->bls_agg_sign_x()).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(common_pk_[block->network_id()].X.c0).c_str());
    return true;
} catch (std::exception& e) {
    ZJC_ERROR("catch bls exception: %s", e.what());
    return false;
}

void GenesisBlockInit::AddBlockItemToCache(
        std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    pools::protobuf::PoolLatestInfo pool_info;
    pool_info.set_height(block->height());
    pool_info.set_hash(block->hash());
    prefix_db_->SaveLatestPoolInfo(
        block->network_id(), block->pool_index(), pool_info, db_batch);
    ZJC_DEBUG("success add pool latest info: %u, %u, %lu", block->network_id(), block->pool_index(), block->height());
}

// 在 net_id 中为 shard 节点创建块
int GenesisBlockInit::CreateShardNodesBlocks(
        std::unordered_map<uint32_t, std::string>& pool_prev_hash_map,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id,
        pools::protobuf::StatisticTxItem& init_heights,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map) {
    std::set<std::string> valid_ids;
    for (auto iter = root_genesis_nodes.begin(); iter != root_genesis_nodes.end(); ++iter) {
        if (valid_ids.find((*iter)->id) != valid_ids.end()) {
            ZJC_FATAL("invalid id: %s", common::Encode::HexEncode((*iter)->id).c_str());
            return kInitError;
        }

        valid_ids.insert((*iter)->id);
    }

    for (auto iter = cons_genesis_nodes.begin(); iter != cons_genesis_nodes.end(); ++iter) {
        if (valid_ids.find((*iter)->id) != valid_ids.end()) {
            ZJC_FATAL("invalid id: %s", common::Encode::HexEncode((*iter)->id).c_str());
            return kInitError;
        }

        valid_ids.insert((*iter)->id);
    }

    // valid_ids 为所有节点（包括 root 和 shard）address

    uint64_t all_balance = 0llu;
    uint64_t expect_all_balance = 0;
    // 统计每个 pool 的链长度
    std::map<uint32_t, uint64_t> pool_height;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        pool_height[i] = init_heights.heights(i);
    }

    // 每个节点分配创世账户余额
    // uint64_t genesis_account_balance = common::kGenesisShardingNodesMaxZjc / valid_ids.size();
    
    
    int32_t idx = 0;
    // 每个节点都要创建一个块
    for (auto iter = valid_ids.begin(); iter != valid_ids.end(); ++iter, ++idx) {
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        std::string address = *iter;

        uint64_t genesis_account_balance = 0;
        auto balance_iter = genesis_acount_balance_map.find(*iter);
        if (balance_iter != genesis_acount_balance_map.end()) {
            genesis_account_balance = balance_iter->second;
            expect_all_balance += genesis_account_balance;
        }

        // 添加创建节点账户交易，节点账户用于选举
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(address);
            tx_info->set_amount(0);
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        // 一个节点地址对应一个 pool index，将 shard 节点的选举交易分配到不同的 pool 中
        auto pool_index = common::GetAddressPoolIndex(address);
        for (uint32_t member_idx = 0; member_idx < cons_genesis_nodes.size(); ++member_idx) {
            if (common::GetAddressPoolIndex(cons_genesis_nodes[member_idx]->id) == pool_index) {
                auto join_elect_tx_info = tx_list->Add();
                join_elect_tx_info->set_step(pools::protobuf::kJoinElect);
                join_elect_tx_info->set_from(cons_genesis_nodes[member_idx]->id);
                join_elect_tx_info->set_to("");
                join_elect_tx_info->set_gas_limit(0);
                join_elect_tx_info->set_gas_used(0);
                join_elect_tx_info->set_status(0);
                auto storage = join_elect_tx_info->add_storages();
                storage->set_key(protos::kJoinElectVerifyG2);
                storage->set_val_hash(cons_genesis_nodes[member_idx]->check_hash);
                storage->set_val_size(33);
                join_elect_tx_info->set_amount(0);
                join_elect_tx_info->set_balance(genesis_account_balance);
            }
        }

        tenon_block->set_prehash(pool_prev_hash_map[pool_index]);
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(pool_index);
        tenon_block->set_electblock_height(1);
        tenon_block->set_height(pool_height[pool_index] + 1);
        pool_height[pool_index] = pool_height[pool_index] + 1;
        tenon_block->set_timeblock_height(0);
        tenon_block->set_electblock_height(1);
        // TODO network 就是 net_id
        tenon_block->set_network_id(net_id);
        tenon_block->set_is_commited_block(true);
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        if (net_id == network::kRootCongressNetworkId) {
            BlsAggSignBlock(root_genesis_nodes, tenon_block);
        } else {
            BlsAggSignBlock(cons_genesis_nodes, tenon_block);
        }

        pool_prev_hash_map[pool_index] = tenon_block->hash();
        //         INIT_DEBUG("add genesis block account id: %s", common::Encode::HexEncode(address).c_str());
        db::DbWriteBatch db_batch;
        pools_mgr_->UpdateLatestInfo(
            0,
            tenon_block->network_id(),
            tenon_block->pool_index(),
            tenon_block->height(),
            tenon_block->hash(),
            tenon_block->prehash(),
            db_batch);
        AddBlockItemToCache(tenon_block, db_batch);
        block_mgr_->GenesisNewBlock(0, tenon_block);
        for (uint32_t i = 0; i < cons_genesis_nodes.size(); ++i) {
            for (int32_t tx_idx = 0; tx_idx < tenon_block->tx_list_size(); ++tx_idx) {
                if (tenon_block->tx_list(tx_idx).step() == pools::protobuf::kJoinElect) {
                    block_mgr_->HandleJoinElectTx(0, *tenon_block, tenon_block->tx_list(tx_idx), db_batch);
                }
            }
        }

        // root 网络节点账户状态都在 shard3 中
        if (net_id == network::kRootCongressNetworkId) {
            block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block, db_batch);
        } else {
            block_mgr_->GenesisAddAllAccount(net_id, tenon_block, db_batch);
        }
        
        db_->Put(db_batch);
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            ZJC_FATAL("get address failed! [%s]", common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != genesis_account_balance) {
            ZJC_FATAL("get address balance failed! [%s]", common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        all_balance += account_ptr->balance();
        init_heights.set_heights(pool_index, tenon_block->height());
    }

    if (all_balance != expect_all_balance) {
        ZJC_FATAL("all_balance != expect_all_balance failed! [%lu][%llu]",
            all_balance, expect_all_balance);
        return kInitError;
    }
    return kInitSuccess;
}

// CreateShardGenesisBlocks 为某 shard 网络创建创世块
// params: 
// root_genesis_nodes root 网络的创世节点
// cons_genesis_nodes 目标 shard 网络的创世节点
// net_id 网络 ID
// genesis_acount_balance_map 节点账户分配余额表
int GenesisBlockInit::CreateShardGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map) {
    // shard 账户
    // InitGenesisAccount();
    InitShardGenesisAccount();
    std::map<uint32_t, std::string> pool_acc_map;
    auto it = net_pool_index_map_.find(net_id);
    if (it != net_pool_index_map_.end()) {
        pool_acc_map = it->second;
    } else {
        return kInitError; 
    }
     
    // 每个账户分配余额，只有 shard3 中的合法账户会被分配
    uint64_t genesis_account_balance = 0;
    if (net_id == network::kConsensusShardBeginNetworkId) {
        genesis_account_balance = common::kGenesisFoundationMaxZjc / pool_acc_map.size();
    }
    pool_acc_map[common::kRootChainPoolIndex] = common::kRootPoolsAddress;
    
    uint64_t all_balance = 0llu;
    pools::protobuf::StatisticTxItem init_heights;
    std::unordered_map<uint32_t, std::string> pool_prev_hash_map;
    
    uint32_t idx = 0;
    // 给每个账户在 net_id 网络中创建块，并分配到不同的 pool 当中
    for (auto iter = pool_acc_map.begin(); iter != pool_acc_map.end(); ++iter, ++idx) {
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        std::string address = iter->second;
        
        // from
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            // TODO 这里不用区分啊，一样的，后面修改看看
            if (idx < common::kImmutablePoolSize) {
                tx_info->set_to(GetValidPoolBaseAddr(common::GetAddressPoolIndex(address)));
            } else {
                // 单独创建 0x000...000
                tx_info->set_to(address);
            }
            
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        // to
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            if (idx < common::kImmutablePoolSize) {
                tx_info->set_from(GetValidPoolBaseAddr(common::GetAddressPoolIndex(address)));
            } else {
                tx_info->set_from(address);
            }

            tx_info->set_to("");
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        if (idx < common::kImmutablePoolSize) {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(address);

            if (net_id == network::kConsensusShardBeginNetworkId && iter->first == common::kImmutablePoolSize - 1) {
                genesis_account_balance += common::kGenesisFoundationMaxZjc % common::kImmutablePoolSize;
            }

            tx_info->set_amount(genesis_account_balance);
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }            
        
        tenon_block->set_prehash("");
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(iter->first);
        tenon_block->set_height(0);
        tenon_block->set_timeblock_height(0);
        tenon_block->set_electblock_height(1);
        tenon_block->set_network_id(net_id);
        tenon_block->set_is_commited_block(true);
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        BlsAggSignBlock(cons_genesis_nodes, tenon_block);
        // 更新所有 pool 的 prehash
        pool_prev_hash_map[iter->first] = tenon_block->hash();

        db::DbWriteBatch db_batch;
        // 更新 pool 最新信息
        pools_mgr_->UpdateLatestInfo(
            0,
            tenon_block->network_id(),
            tenon_block->pool_index(),
            tenon_block->height(),
            tenon_block->hash(),
            tenon_block->prehash(),
            db_batch);
        AddBlockItemToCache(tenon_block, db_batch);
        block_mgr_->GenesisNewBlock(0, tenon_block);
        block_mgr_->GenesisAddAllAccount(net_id, tenon_block, db_batch);
        db_->Put(db_batch);

        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            ZJC_FATAL("get address failed! [%s]", common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (idx < common::kImmutablePoolSize) {
            if (account_ptr->balance() != genesis_account_balance) {
                ZJC_FATAL("get address balance failed! [%s]", common::Encode::HexEncode(address).c_str());
                return kInitError;
            }
        } else {
            if (account_ptr->balance() != 0) {
                ZJC_FATAL("get address balance failed! [%s]", common::Encode::HexEncode(address).c_str());
                return kInitError;
            }
        }

        all_balance += account_ptr->balance();    

        if (net_id != network::kConsensusShardBeginNetworkId) {
            all_balance = common::kGenesisFoundationMaxZjc;
        }
        
        init_heights.add_heights(0);
    }

    if (all_balance != common::kGenesisFoundationMaxZjc) {
        ZJC_FATAL("all_balance != common::kGenesisFoundationMaxTenon failed! [%lu][%llu]",
            all_balance, common::kGenesisFoundationMaxZjc);
        return kInitError;
    }

    CreateShardNodesBlocks(pool_prev_hash_map,
                           root_genesis_nodes,
                           cons_genesis_nodes,
                           net_id,
                           init_heights,
                           genesis_acount_balance_map);
    prefix_db_->SaveStatisticLatestHeihgts(net_id, init_heights);
    std::string init_consensus_height;
    for (int32_t i = 0; i < init_heights.heights_size(); ++i) {
        init_consensus_height += std::to_string(init_heights.heights(i)) + " ";
    }

    ZJC_DEBUG("0 success change min elect statistic heights: %u, %s",
        net_id, init_consensus_height.c_str());
    // 通过文件同步 RootSingleBlock
    // 包含 root 网络中的 root pool 账户、选举块和时间块
    return GenerateShardSingleBlock(net_id);
}

// GetNetworkIdOfGenesisAddress 根据创世账户地址分配 network_id
// TODO 目前默认是 shard 3
uint32_t GenesisBlockInit::GetNetworkIdOfGenesisAddress(const std::string& address) {
    return network::kConsensusShardBeginNetworkId;
}

// InitShardGenesisAccount 初始化所有 shard 所有 pool 的创世账号，共 64 x 256 个
void GenesisBlockInit::InitShardGenesisAccount() {
    // Execute once
    static bool hasRunOnce = false;

    if (!hasRunOnce) {
        for (uint32_t net_id = network::kConsensusShardBeginNetworkId; net_id < network::kConsensusShardEndNetworkId; net_id++) {
            // 除了 shard3，其他 shard 的创世账户都是一些随机假账户
        
            if (net_id == network::kConsensusShardBeginNetworkId) {
                InitGenesisAccount();
                net_pool_index_map_.insert(std::make_pair(net_id, pool_index_map_));
            } else {
                std::map<uint32_t, std::string> tmp_pool_map_;
                for (uint32_t pool_id = 0; pool_id < 256; pool_id++) {       
                    tmp_pool_map_.insert(std::make_pair(pool_id, common::Encode::HexDecode(common::Random::RandomString(20))));

                }
                net_pool_index_map_.insert(std::make_pair(net_id, tmp_pool_map_));
            }
        }    
    }

    hasRunOnce = true;
}

void GenesisBlockInit::InitGenesisAccount() {
    {
        pool_index_map_.insert(std::make_pair(0, common::Encode::HexDecode("cf38155c0dc3c768f58360072f59e78403116b08")));
        pool_index_map_.insert(std::make_pair(1, common::Encode::HexDecode("9af7be6ba99d053a286667e673710ee2d7b2599a")));
        pool_index_map_.insert(std::make_pair(2, common::Encode::HexDecode("04b8d10872ef7acd83bcdcb594821ab50982f7da")));
        pool_index_map_.insert(std::make_pair(3, common::Encode::HexDecode("238031bb79bfc022eaa328be6d242e46d8cb0002")));
        pool_index_map_.insert(std::make_pair(4, common::Encode::HexDecode("f48a947f1f919cd7028abb77b10181403c9da730")));
        pool_index_map_.insert(std::make_pair(5, common::Encode::HexDecode("01bba0adf8e08df35fb77ae4d5338be727a94bba")));
        pool_index_map_.insert(std::make_pair(6, common::Encode::HexDecode("ca0c1003bdd5636b48be2fb9f8c7d89942190705")));
        pool_index_map_.insert(std::make_pair(7, common::Encode::HexDecode("bb74e40ba9c4d5eb8c0b444231f5c42e8ff5cd9a")));
        pool_index_map_.insert(std::make_pair(8, common::Encode::HexDecode("35675a328435e247e509e5645d9ce9927a64e746")));
        pool_index_map_.insert(std::make_pair(9, common::Encode::HexDecode("aaed501a62acd79403fc354ed7b7bd3dc8289e7a")));
        pool_index_map_.insert(std::make_pair(10, common::Encode::HexDecode("1e5d2478593ba3f94bb29649d2dd7d57328c2318")));
        pool_index_map_.insert(std::make_pair(11, common::Encode::HexDecode("f611dbb360ed0196e2ff24617cb31579956d45d5")));
        pool_index_map_.insert(std::make_pair(12, common::Encode::HexDecode("51fd441cac2c71bf20aaab97a9468441fba8c632")));
        pool_index_map_.insert(std::make_pair(13, common::Encode::HexDecode("df2e221c02b966bf5ada729f7e730b7a4092e0a3")));
        pool_index_map_.insert(std::make_pair(14, common::Encode::HexDecode("62211a8e3a054138b9ad41dc16b8e4a6af1db4b6")));
        pool_index_map_.insert(std::make_pair(15, common::Encode::HexDecode("048fd3e168ce69844f95e6714d4b7ca846de2048")));
        pool_index_map_.insert(std::make_pair(16, common::Encode::HexDecode("974f404a1d9c5ded78e25cb67329144e85e64a24")));
        pool_index_map_.insert(std::make_pair(17, common::Encode::HexDecode("ed7c25783e1962a67659c34ea018b8b68ee52ff7")));
        pool_index_map_.insert(std::make_pair(18, common::Encode::HexDecode("02ad4c02141951b5d3f1d8d5aa8920e632d90c25")));
        pool_index_map_.insert(std::make_pair(19, common::Encode::HexDecode("d4e461225dfea5d2950910c3ddc3c94366f3278f")));
        pool_index_map_.insert(std::make_pair(20, common::Encode::HexDecode("63511724c6045b8e7cecd4cfaf2be2e5e622c4b6")));
        pool_index_map_.insert(std::make_pair(21, common::Encode::HexDecode("7db4d285b90eac2e7ae5356f6627214422ea4a03")));
        pool_index_map_.insert(std::make_pair(22, common::Encode::HexDecode("c7f118db680fb7cd08f5f4156922cb59d44523cb")));
        pool_index_map_.insert(std::make_pair(23, common::Encode::HexDecode("7084d9f2209a938450a331be568bee4ec5b0b306")));
        pool_index_map_.insert(std::make_pair(24, common::Encode::HexDecode("0673a8b7ccd448b6282df0454d1a2b84408d44de")));
        pool_index_map_.insert(std::make_pair(25, common::Encode::HexDecode("afd151f711b44b2f9a835b6eb52f0d7baf7b9328")));
        pool_index_map_.insert(std::make_pair(26, common::Encode::HexDecode("1583b068b4faa0cfe67168bba615fb06f4d67565")));
        pool_index_map_.insert(std::make_pair(27, common::Encode::HexDecode("1946ed140f4f51f57792defcefc5bed7f76ca173")));
        pool_index_map_.insert(std::make_pair(28, common::Encode::HexDecode("740ee2433e2c263c590c83ae40a8a82f4ba87b24")));
        pool_index_map_.insert(std::make_pair(29, common::Encode::HexDecode("0c1e9f2c0a5234205dcbd55eb1896e9040b4e40f")));
        pool_index_map_.insert(std::make_pair(30, common::Encode::HexDecode("b8ec103674b71d837a2b5cd5b9347255b7dc5733")));
        pool_index_map_.insert(std::make_pair(31, common::Encode::HexDecode("d3dfc2eec7d0fa7c89513287e46e9c88e600f417")));
        pool_index_map_.insert(std::make_pair(32, common::Encode::HexDecode("97688d36eaa0a6f2960f77ed6128d1d7ad278adc")));
        pool_index_map_.insert(std::make_pair(33, common::Encode::HexDecode("49d73ec368949d162f2623053bdc79f8b1148006")));
        pool_index_map_.insert(std::make_pair(34, common::Encode::HexDecode("46c39946c9a298e029b04688f8b571da3ad7da99")));
        pool_index_map_.insert(std::make_pair(35, common::Encode::HexDecode("1f1fea9d8237663b3d8fa13d373de46a37adcd95")));
        pool_index_map_.insert(std::make_pair(36, common::Encode::HexDecode("fe7a1ac6b12ce5f1cc4c52eaac5d138cc3135003")));
        pool_index_map_.insert(std::make_pair(37, common::Encode::HexDecode("1c445eb6e7f32d41fe43f59f0d24a6bd64586d7f")));
        pool_index_map_.insert(std::make_pair(38, common::Encode::HexDecode("c7402ed4160bc9c5a8c011c87b57671e4c5c713a")));
        pool_index_map_.insert(std::make_pair(39, common::Encode::HexDecode("bd94ddb1756187c9f540e95410aadcb8a6ff640b")));
        pool_index_map_.insert(std::make_pair(40, common::Encode::HexDecode("6f7a3ebd3a8a37d452141e3278ab9ad5a8210eb7")));
        pool_index_map_.insert(std::make_pair(41, common::Encode::HexDecode("929df8b83181b027be3a23eb5fa98d6ce7003fd3")));
        pool_index_map_.insert(std::make_pair(42, common::Encode::HexDecode("ab3f6b57ff94167d8513d8c95d5da76f01e88c59")));
        pool_index_map_.insert(std::make_pair(43, common::Encode::HexDecode("42769b189f643cb55b69656d36593617b24e0599")));
        pool_index_map_.insert(std::make_pair(44, common::Encode::HexDecode("2e368a598cf8106a42e448ccfcc70d2488523a75")));
        pool_index_map_.insert(std::make_pair(45, common::Encode::HexDecode("5ae7729b777d8fd6ee3a20ec58e89bd94d3d92a3")));
        pool_index_map_.insert(std::make_pair(46, common::Encode::HexDecode("ae089998e96869242bfc00a7b3bd15c6adde98ae")));
        pool_index_map_.insert(std::make_pair(47, common::Encode::HexDecode("0bf298fa5b6ead3901a02b948fe2761dc65ecae4")));
        pool_index_map_.insert(std::make_pair(48, common::Encode::HexDecode("99c32514d42b0a62bef8762ea123d2e0127af6e6")));
        pool_index_map_.insert(std::make_pair(49, common::Encode::HexDecode("be7f6317af47de8437dd12a23592fca0289bd8a4")));
        pool_index_map_.insert(std::make_pair(50, common::Encode::HexDecode("64ca43f886f79503b8a082b3b9b509722d7e1cd8")));
        pool_index_map_.insert(std::make_pair(51, common::Encode::HexDecode("a9d8250460e626b4da7b57f4135f7e7fa92b60d5")));
        pool_index_map_.insert(std::make_pair(52, common::Encode::HexDecode("5d9d780ea31b069c700e5263ac39cd5af969e5b4")));
        pool_index_map_.insert(std::make_pair(53, common::Encode::HexDecode("b4970715f73984248a1ca17f90e70b3e4ee0d26e")));
        pool_index_map_.insert(std::make_pair(54, common::Encode::HexDecode("5cc04ac097031594ee80d443b7921a81b0ca0ed3")));
        pool_index_map_.insert(std::make_pair(55, common::Encode::HexDecode("ba791554a589e3d83822c284fe5c6a6ed78365ed")));
        pool_index_map_.insert(std::make_pair(56, common::Encode::HexDecode("21d057ee8475c8513b6c966a7d1b0f68ea9411f5")));
        pool_index_map_.insert(std::make_pair(57, common::Encode::HexDecode("12ce953a652df271a1374fcb2b11f1d413852ec9")));
        pool_index_map_.insert(std::make_pair(58, common::Encode::HexDecode("48d0e92c5b7046ded2aba8455130fda39c8cb64b")));
        pool_index_map_.insert(std::make_pair(59, common::Encode::HexDecode("499a914f1d44c7e41467a2f28a204b9dcb6cf73d")));
        pool_index_map_.insert(std::make_pair(60, common::Encode::HexDecode("db54aa8e34aa171bb548b860b52c06cd6cdf1952")));
        pool_index_map_.insert(std::make_pair(61, common::Encode::HexDecode("73392d1fe57cabcfa2a41f612fae3bc68bcc3e68")));
        pool_index_map_.insert(std::make_pair(62, common::Encode::HexDecode("883050a31c45e6f932b58d13cac5612cd2b9f514")));
        pool_index_map_.insert(std::make_pair(63, common::Encode::HexDecode("b181df4d60d01e2489e0c7478dc360e47d171746")));
        pool_index_map_.insert(std::make_pair(64, common::Encode::HexDecode("fedcce274f73a8388b19320216deea09e4306979")));
        pool_index_map_.insert(std::make_pair(65, common::Encode::HexDecode("2bd4ba479cbe5cbe6740301473f561c485d76dc1")));
        pool_index_map_.insert(std::make_pair(66, common::Encode::HexDecode("56c7433e952f25da903918d65ce223d1794bc501")));
        pool_index_map_.insert(std::make_pair(67, common::Encode::HexDecode("d4908c9ac25e78dcf6a77b87bc829bd8dfd44d68")));
        pool_index_map_.insert(std::make_pair(68, common::Encode::HexDecode("e36dbbcdff6c40de80a6350026f08ec3fccc3688")));
        pool_index_map_.insert(std::make_pair(69, common::Encode::HexDecode("d1fb620b38e72a48f61827df33da3b39ecc42653")));
        pool_index_map_.insert(std::make_pair(70, common::Encode::HexDecode("9cd403a44f1518ffcfd83f81aa1100934ad601e9")));
        pool_index_map_.insert(std::make_pair(71, common::Encode::HexDecode("3abfe6d33fe98f4ea67cf84e8b6cd3b6d1942a66")));
        pool_index_map_.insert(std::make_pair(72, common::Encode::HexDecode("0c71365a004774fdb6855a9a69e762411454aecd")));
        pool_index_map_.insert(std::make_pair(73, common::Encode::HexDecode("8e20fdc33dd51b2dcd57e4fb1af2d7b02a176565")));
        pool_index_map_.insert(std::make_pair(74, common::Encode::HexDecode("72a73b65c565c0e1963b47ab6606f667981c5362")));
        pool_index_map_.insert(std::make_pair(75, common::Encode::HexDecode("173ead6cc50a8cf649ca55b9a8f6051e2618f4d2")));
        pool_index_map_.insert(std::make_pair(76, common::Encode::HexDecode("63dda271fab63d901561196d02001b323240ccda")));
        pool_index_map_.insert(std::make_pair(77, common::Encode::HexDecode("e21a2abdf774eda8206ea8c9813beb637dd44414")));
        pool_index_map_.insert(std::make_pair(78, common::Encode::HexDecode("9891bf255c110dff765b4e0c353d4d3b5a77f7b6")));
        pool_index_map_.insert(std::make_pair(79, common::Encode::HexDecode("13745fc4b967d22f9889605c68b2e627d5f35367")));
        pool_index_map_.insert(std::make_pair(80, common::Encode::HexDecode("5440972d73564688eaf2923f069f73d4bc298518")));
        pool_index_map_.insert(std::make_pair(81, common::Encode::HexDecode("cbf01ae389df0b2439658e4777e0d385c08537a4")));
        pool_index_map_.insert(std::make_pair(82, common::Encode::HexDecode("e7113402cc9ee6b5a226b74e38ee97483934f349")));
        pool_index_map_.insert(std::make_pair(83, common::Encode::HexDecode("3c50d3cf6f3b0acd4ec98695490f1c0068fd5151")));
        pool_index_map_.insert(std::make_pair(84, common::Encode::HexDecode("2300011b3b74b4f0c6f02099126969dadd8595aa")));
        pool_index_map_.insert(std::make_pair(85, common::Encode::HexDecode("b37c1a807f225d60f6014037e6ac9f0cb426137d")));
        pool_index_map_.insert(std::make_pair(86, common::Encode::HexDecode("4e2908241f6b77f29de23ac0bc3c99280d06f175")));
        pool_index_map_.insert(std::make_pair(87, common::Encode::HexDecode("eabf60bfb1638568780c1bb8c70e2c42872c5b91")));
        pool_index_map_.insert(std::make_pair(88, common::Encode::HexDecode("67243059b404f4e2928615904b5e72e4bcc1f329")));
        pool_index_map_.insert(std::make_pair(89, common::Encode::HexDecode("a2c16e2f1851240422b9de0ce7710a5d5c2ae094")));
        pool_index_map_.insert(std::make_pair(90, common::Encode::HexDecode("e54394dfed9265b60b8bd1aef29d5b0475d2531c")));
        pool_index_map_.insert(std::make_pair(91, common::Encode::HexDecode("1e650bbcb2959b3a187d79c151c75584e8844be2")));
        pool_index_map_.insert(std::make_pair(92, common::Encode::HexDecode("528daf87753b75450f81543a7894b45507324fc1")));
        pool_index_map_.insert(std::make_pair(93, common::Encode::HexDecode("e4dd3c5ed5e50e4fcdb4231b33c7a22b0930aaac")));
        pool_index_map_.insert(std::make_pair(94, common::Encode::HexDecode("2bee9f73e18833218f70949d3130349fcedacc1c")));
        pool_index_map_.insert(std::make_pair(95, common::Encode::HexDecode("16375d42a2a03f7597a26ea45e2488515b7f056e")));
        pool_index_map_.insert(std::make_pair(96, common::Encode::HexDecode("0e7f34d6d6e01621dbb7a81a8e06ef41dfe1a357")));
        pool_index_map_.insert(std::make_pair(97, common::Encode::HexDecode("14f8d3d28e41f52bc6abc62e6ce23e0201cc6e39")));
        pool_index_map_.insert(std::make_pair(98, common::Encode::HexDecode("1ca6207055297d75d4d78b8700608e8f21efc716")));
        pool_index_map_.insert(std::make_pair(99, common::Encode::HexDecode("853ce2d0fb64439ca45605651ac3ff209a367150")));
        pool_index_map_.insert(std::make_pair(100, common::Encode::HexDecode("ac2dff3a351adc9cbc9a0a25c13f82a96fbcac0f")));
        pool_index_map_.insert(std::make_pair(101, common::Encode::HexDecode("db1887239ae8bdd7b5de67b493a6fb5eca87ada8")));
        pool_index_map_.insert(std::make_pair(102, common::Encode::HexDecode("c81fd59e2c700681891e235575ed428170e4da3a")));
        pool_index_map_.insert(std::make_pair(103, common::Encode::HexDecode("5404649aa3601f50d9c36fefa4eb2b9f4afe541a")));
        pool_index_map_.insert(std::make_pair(104, common::Encode::HexDecode("92c37672d1cc74f6e4c28d02f506d4d86d77dab3")));
        pool_index_map_.insert(std::make_pair(105, common::Encode::HexDecode("4cdc55c340a3d17a8630968a884d6a7399264595")));
        pool_index_map_.insert(std::make_pair(106, common::Encode::HexDecode("e7369ab49054b2b686cf8873124bcfc2a7956392")));
        pool_index_map_.insert(std::make_pair(107, common::Encode::HexDecode("73fb5bb7b2cdf74173c2bb0a997e4b004553dbee")));
        pool_index_map_.insert(std::make_pair(108, common::Encode::HexDecode("e1f3b638d69804d183da92512f427230b957a537")));
        pool_index_map_.insert(std::make_pair(109, common::Encode::HexDecode("eb1dfc764476acd2733705d664edf03370e10e2e")));
        pool_index_map_.insert(std::make_pair(110, common::Encode::HexDecode("c38743c146c57a0f0e1c55da0f7cd743e6e1f0d9")));
        pool_index_map_.insert(std::make_pair(111, common::Encode::HexDecode("745d16d2ca65958fbd320041298b053160a31b36")));
        pool_index_map_.insert(std::make_pair(112, common::Encode::HexDecode("9334d49f293b16c018f0c3f854ee3142001cf34b")));
        pool_index_map_.insert(std::make_pair(113, common::Encode::HexDecode("739c2d024f7e4a63cc5e8ce74a9eca783f1b83bf")));
        pool_index_map_.insert(std::make_pair(114, common::Encode::HexDecode("6c0a59c0542a5077b20c95d4a0dd3f91f95c56f0")));
        pool_index_map_.insert(std::make_pair(115, common::Encode::HexDecode("a07fd3728010dffbbca73a3d3617e73ed6807f5b")));
        pool_index_map_.insert(std::make_pair(116, common::Encode::HexDecode("05a809937ef7c2cdb432922b15cec828c83c460d")));
        pool_index_map_.insert(std::make_pair(117, common::Encode::HexDecode("1e1696e05baba49ce2bda4b2aa971cf7fc7d68b4")));
        pool_index_map_.insert(std::make_pair(118, common::Encode::HexDecode("a3044e29fac9e73dc6016bb0e45a88b951565856")));
        pool_index_map_.insert(std::make_pair(119, common::Encode::HexDecode("673cf13182ded7753258d57fda6685fa9fb3a31a")));
        pool_index_map_.insert(std::make_pair(120, common::Encode::HexDecode("1daf64cb37a87df8a8f33ebdfd645ed6172c9cdd")));
        pool_index_map_.insert(std::make_pair(121, common::Encode::HexDecode("0f3f19189569fdf88efdfac9f062a733bb1e8447")));
        pool_index_map_.insert(std::make_pair(122, common::Encode::HexDecode("0f54e9b388e23e8974c0bc0a0d37d0129cca97d9")));
        pool_index_map_.insert(std::make_pair(123, common::Encode::HexDecode("90a5966a10c619b652c924b60d3ee05201fceae3")));
        pool_index_map_.insert(std::make_pair(124, common::Encode::HexDecode("f595a6298f3f5e6cd1e17e4e9dbd808a4f03bcee")));
        pool_index_map_.insert(std::make_pair(125, common::Encode::HexDecode("40800b7cef19d7bf24e96536d8c06a54ef4f104b")));
        pool_index_map_.insert(std::make_pair(126, common::Encode::HexDecode("8de6af3670adc150daf9ba8666ac3bd7a89ed6c5")));
        pool_index_map_.insert(std::make_pair(127, common::Encode::HexDecode("9450f5ba1032249b9114802ff324ddd62864bb92")));
        pool_index_map_.insert(std::make_pair(128, common::Encode::HexDecode("307fd90bb6c3eb293f064a8affe0ff0f26b31778")));
        pool_index_map_.insert(std::make_pair(129, common::Encode::HexDecode("ce0675a76ee90bf8d4cd1083706a3db50f4b597a")));
        pool_index_map_.insert(std::make_pair(130, common::Encode::HexDecode("cf22c7ad934af4d5b91cb382ed03f28057e5d9c9")));
        pool_index_map_.insert(std::make_pair(131, common::Encode::HexDecode("01de22ced6220bd247a6a06975dee1ab3cfe24ab")));
        pool_index_map_.insert(std::make_pair(132, common::Encode::HexDecode("f1ad8bcc0f23c8edbf916d775cf6dd389c3cc3fb")));
        pool_index_map_.insert(std::make_pair(133, common::Encode::HexDecode("df6a9b88bf693d7a40e0d86635090873844d96e6")));
        pool_index_map_.insert(std::make_pair(134, common::Encode::HexDecode("5b7a20837c28a7a5b7ec6cfa1dbf8d8311b9ed1a")));
        pool_index_map_.insert(std::make_pair(135, common::Encode::HexDecode("3c47b2ef956a5bc4994f7cde8bfb1354149a4c25")));
        pool_index_map_.insert(std::make_pair(136, common::Encode::HexDecode("2f11ad89c51b5b0a7e68f64f103986fb731bedda")));
        pool_index_map_.insert(std::make_pair(137, common::Encode::HexDecode("3290cc419c5d420518813361781f9ecd13089b7a")));
        pool_index_map_.insert(std::make_pair(138, common::Encode::HexDecode("10d3a1b647af7df68ad2a2fd3a01f9b5dca4d98c")));
        pool_index_map_.insert(std::make_pair(139, common::Encode::HexDecode("04e4e978c5b6a3e31747e140efb51dc458691f1f")));
        pool_index_map_.insert(std::make_pair(140, common::Encode::HexDecode("9d4a8349efec4ed7178efa2746312c538a85f8ee")));
        pool_index_map_.insert(std::make_pair(141, common::Encode::HexDecode("5ad2c9b8d8363fd7b87e2350141104181ec226b2")));
        pool_index_map_.insert(std::make_pair(142, common::Encode::HexDecode("29b4de2d15e2378fa894d96e8a7d2826bfd574ca")));
        pool_index_map_.insert(std::make_pair(143, common::Encode::HexDecode("d438ea8e542b3930de41069f50939c2f781b0a56")));
        pool_index_map_.insert(std::make_pair(144, common::Encode::HexDecode("7cbf564e26dfe8196ab79779a3d7d371971777a4")));
        pool_index_map_.insert(std::make_pair(145, common::Encode::HexDecode("a56a406c17fce17bf9f16895d647faa664ed3ccb")));
        pool_index_map_.insert(std::make_pair(146, common::Encode::HexDecode("a418630e8ab12513b29e67f7b6c77662f5195f73")));
        pool_index_map_.insert(std::make_pair(147, common::Encode::HexDecode("b20098819084e082b90b2c29c14d60e1ec350fe6")));
        pool_index_map_.insert(std::make_pair(148, common::Encode::HexDecode("398e6158dfecdba6e8ec49819f281ea6d64fb28f")));
        pool_index_map_.insert(std::make_pair(149, common::Encode::HexDecode("a549b0d67cf86972cfd1d75d7f0df2618d9ad429")));
        pool_index_map_.insert(std::make_pair(150, common::Encode::HexDecode("5ee11d358e1034faf665706fe3bbc2e0a27f5e86")));
        pool_index_map_.insert(std::make_pair(151, common::Encode::HexDecode("72810a06fd6eba3741f164ccb56931f7b9103863")));
        pool_index_map_.insert(std::make_pair(152, common::Encode::HexDecode("6047342c6c83d2a5f4c18519c8eda87a7dfada62")));
        pool_index_map_.insert(std::make_pair(153, common::Encode::HexDecode("f97d24a2d359352c344086cb8da1ff283e452f0b")));
        pool_index_map_.insert(std::make_pair(154, common::Encode::HexDecode("7666dfa3344688817653887a50362b1af61bf361")));
        pool_index_map_.insert(std::make_pair(155, common::Encode::HexDecode("56ace3d0af2fb967d4360175b18ac62d078856f9")));
        pool_index_map_.insert(std::make_pair(156, common::Encode::HexDecode("65d6b770375a317ffced1c7f94dced4ee1c54e5b")));
        pool_index_map_.insert(std::make_pair(157, common::Encode::HexDecode("ceec5b5b9425473946807906e98fc73740a41cdb")));
        pool_index_map_.insert(std::make_pair(158, common::Encode::HexDecode("2c500ae5bb7d3f38c200861ddbb1e578d3af5d67")));
        pool_index_map_.insert(std::make_pair(159, common::Encode::HexDecode("1026f02792f0bd87af485304d88183ab664cfbd2")));
        pool_index_map_.insert(std::make_pair(160, common::Encode::HexDecode("26902c1e27781c51861f49e745cfe196690b00f1")));
        pool_index_map_.insert(std::make_pair(161, common::Encode::HexDecode("edeb7d0d5543f72f9d9249e35f70f56b1e33192e")));
        pool_index_map_.insert(std::make_pair(162, common::Encode::HexDecode("14794ecd05a051af78420765ef29668e8a1b652a")));
        pool_index_map_.insert(std::make_pair(163, common::Encode::HexDecode("c2689a9f2551581ba295238a682e1e72bd583ca1")));
        pool_index_map_.insert(std::make_pair(164, common::Encode::HexDecode("fe28b39d7c6493b52aeaf75a7219ccc387199da2")));
        pool_index_map_.insert(std::make_pair(165, common::Encode::HexDecode("8233098f6f19e99f77b1a72eb93ba9ef7c07e6b0")));
        pool_index_map_.insert(std::make_pair(166, common::Encode::HexDecode("8868bff7b3e26ad09f3969798bc828ffcad73e60")));
        pool_index_map_.insert(std::make_pair(167, common::Encode::HexDecode("12bef883c6916b13ecd98b855abe24be6b7298d8")));
        pool_index_map_.insert(std::make_pair(168, common::Encode::HexDecode("f131cd5ec60d604ccba5234f5aea8829054af90e")));
        pool_index_map_.insert(std::make_pair(169, common::Encode::HexDecode("ff1016527a306c5328c160d2f8584046edad6ab2")));
        pool_index_map_.insert(std::make_pair(170, common::Encode::HexDecode("977bfb1e9316d44e0dc33109b12581e38bdd00a6")));
        pool_index_map_.insert(std::make_pair(171, common::Encode::HexDecode("081c9bfe8bacb35b0f9d010147eed64eb1ada0da")));
        pool_index_map_.insert(std::make_pair(172, common::Encode::HexDecode("b27e429a982d73310e48c137d57dead70bf9a118")));
        pool_index_map_.insert(std::make_pair(173, common::Encode::HexDecode("04776fbc94fae97686c8e0cf7f46731efe2017e9")));
        pool_index_map_.insert(std::make_pair(174, common::Encode::HexDecode("9097b0e6667380dd5e5916dad38169b8ef598f6f")));
        pool_index_map_.insert(std::make_pair(175, common::Encode::HexDecode("ee0f0499982d5bdfbedd767d576264aaf53dbb6f")));
        pool_index_map_.insert(std::make_pair(176, common::Encode::HexDecode("e10872125940f89f48f9efefeb9ee263aef6d186")));
        pool_index_map_.insert(std::make_pair(177, common::Encode::HexDecode("343e6c6b5d96cd9fff62229c7da47fe33e8ea719")));
        pool_index_map_.insert(std::make_pair(178, common::Encode::HexDecode("eb433f73a3de0fccde89598f6f054ea769f408f1")));
        pool_index_map_.insert(std::make_pair(179, common::Encode::HexDecode("f07032cae97687c791383e554735431b4a12d506")));
        pool_index_map_.insert(std::make_pair(180, common::Encode::HexDecode("440ed23037c535079f2f291ac66f02a25be1c545")));
        pool_index_map_.insert(std::make_pair(181, common::Encode::HexDecode("bef5d8b250e90f2adcb25273ba6dfd6b3f824186")));
        pool_index_map_.insert(std::make_pair(182, common::Encode::HexDecode("51a8608ba6258d02ecb0cd4363c796b7862a185c")));
        pool_index_map_.insert(std::make_pair(183, common::Encode::HexDecode("8fd01683ad1502132022a6f8e18d2a8abe7aa720")));
        pool_index_map_.insert(std::make_pair(184, common::Encode::HexDecode("54e0dd897c04c038fd8d91ffc48e5b5a0ce29b77")));
        pool_index_map_.insert(std::make_pair(185, common::Encode::HexDecode("13c28a5c48824b0f761d656b75707e218852984c")));
        pool_index_map_.insert(std::make_pair(186, common::Encode::HexDecode("8a8bfcdc1cf80ddeabb6b81347cf73db96b5d996")));
        pool_index_map_.insert(std::make_pair(187, common::Encode::HexDecode("3d8c3c76fa2b254cd6e2253a99cb33be644b752f")));
        pool_index_map_.insert(std::make_pair(188, common::Encode::HexDecode("cc8e5d64e30db41acb3e194cb1d1ba9784d402ad")));
        pool_index_map_.insert(std::make_pair(189, common::Encode::HexDecode("2fe227b2365ec75ecb4ea4b4ddf9caec5ab868fd")));
        pool_index_map_.insert(std::make_pair(190, common::Encode::HexDecode("48669eea54b49114e03e95e0c52da2fbe02debb3")));
        pool_index_map_.insert(std::make_pair(191, common::Encode::HexDecode("bfaf0b9b72160e427116531f3e62be542389ba4f")));
        pool_index_map_.insert(std::make_pair(192, common::Encode::HexDecode("1a831ab0c499de4f3857a2b0fec4ec558c0ea7e0")));
        pool_index_map_.insert(std::make_pair(193, common::Encode::HexDecode("1b07496bb4e8bfe781456f9fb9726c03f809233a")));
        pool_index_map_.insert(std::make_pair(194, common::Encode::HexDecode("81282b7231ea849d0d3d15e32839bce2f983c59e")));
        pool_index_map_.insert(std::make_pair(195, common::Encode::HexDecode("d954450803ef6cc1a1a94b4f7fbb0804c0b541fd")));
        pool_index_map_.insert(std::make_pair(196, common::Encode::HexDecode("e34831a9bce74fd13f9463d8e71e3805f03c706e")));
        pool_index_map_.insert(std::make_pair(197, common::Encode::HexDecode("78be120af4ba3815d2e0253a55cc037007b737fa")));
        pool_index_map_.insert(std::make_pair(198, common::Encode::HexDecode("f3b7c0f1de181335bc5e579d2ce8a83893045b8c")));
        pool_index_map_.insert(std::make_pair(199, common::Encode::HexDecode("2e71340953f1fa9ed65691531702853fba86e371")));
        pool_index_map_.insert(std::make_pair(200, common::Encode::HexDecode("7ea13de18338fb3e2d2ada32aff61c1a4f4b6da6")));
        pool_index_map_.insert(std::make_pair(201, common::Encode::HexDecode("0dcbdd68ef1ca41fa5be185f10d57fc1f51e36f6")));
        pool_index_map_.insert(std::make_pair(202, common::Encode::HexDecode("37753cff61d4b2eee9b5013bfe021bbc9762a502")));
        pool_index_map_.insert(std::make_pair(203, common::Encode::HexDecode("5347dfe5c7d55af87e88655822b2f1a99a53f796")));
        pool_index_map_.insert(std::make_pair(204, common::Encode::HexDecode("68d49ac1f8557e6ffba974380473472096d5d256")));
        pool_index_map_.insert(std::make_pair(205, common::Encode::HexDecode("1778e23dd4589db366754d2deb603ee3a8f99c77")));
        pool_index_map_.insert(std::make_pair(206, common::Encode::HexDecode("881c98af1f5f2881ca594d119ae2b375f2fdb604")));
        pool_index_map_.insert(std::make_pair(207, common::Encode::HexDecode("179a7b042ebe26dba853bbc1e13531aa61dff554")));
        pool_index_map_.insert(std::make_pair(208, common::Encode::HexDecode("7a7b6f8e17c944dda389437a2a5942f5e7a5b9a3")));
        pool_index_map_.insert(std::make_pair(209, common::Encode::HexDecode("1149c3cef29c601843c831207ff850f7295a77a9")));
        pool_index_map_.insert(std::make_pair(210, common::Encode::HexDecode("1112ee5f354d55e573595e4b2b96c3063ad29ab8")));
        pool_index_map_.insert(std::make_pair(211, common::Encode::HexDecode("712aa00368bd9db96ce3c6875edf7ed9c4b467c7")));
        pool_index_map_.insert(std::make_pair(212, common::Encode::HexDecode("d3a9ebbb4cf7e4df6d9f6aa32bc2f824ec465e9e")));
        pool_index_map_.insert(std::make_pair(213, common::Encode::HexDecode("5edc26a94d9240c124ebf0d18641c13ac0f9360a")));
        pool_index_map_.insert(std::make_pair(214, common::Encode::HexDecode("3dde580919f2da90bf081e82cd90bc7c13f5988c")));
        pool_index_map_.insert(std::make_pair(215, common::Encode::HexDecode("c30f31fb03911ef9a3079d2676f0d71ec2f39416")));
        pool_index_map_.insert(std::make_pair(216, common::Encode::HexDecode("616ab42de2f594be2268a68e2e1331377e128168")));
        pool_index_map_.insert(std::make_pair(217, common::Encode::HexDecode("8d4958747289a673eeec563db126653feb0ed209")));
        pool_index_map_.insert(std::make_pair(218, common::Encode::HexDecode("34f9dc9a73f3c93c97d490b783d23f6fc902ee79")));
        pool_index_map_.insert(std::make_pair(219, common::Encode::HexDecode("5ab1666aa16a0fbe0b4712313dfc678a552512e8")));
        pool_index_map_.insert(std::make_pair(220, common::Encode::HexDecode("db886a0f1c04ac9c4624a9423513888169207a16")));
        pool_index_map_.insert(std::make_pair(221, common::Encode::HexDecode("745b7ea7e83ab8d500ef7e7ef61bc091950a86d3")));
        pool_index_map_.insert(std::make_pair(222, common::Encode::HexDecode("c0aa056b5777e4f58358eeebcf43b511dd47f59a")));
        pool_index_map_.insert(std::make_pair(223, common::Encode::HexDecode("963ddfd474ce4b61df1a53329dc634cb55e75f14")));
        pool_index_map_.insert(std::make_pair(224, common::Encode::HexDecode("be19f82d1b3e3fde2d5e6cd009c2071947c78526")));
        pool_index_map_.insert(std::make_pair(225, common::Encode::HexDecode("d1616e29d3198050bf585f6296ea368dec78d953")));
        pool_index_map_.insert(std::make_pair(226, common::Encode::HexDecode("374137a19eb7e96664bbf7aedf15b4ffa10b5528")));
        pool_index_map_.insert(std::make_pair(227, common::Encode::HexDecode("7f68827917cfc224f56c65f7bbac34a541ddc8cd")));
        pool_index_map_.insert(std::make_pair(228, common::Encode::HexDecode("c600d389819f23177f3d9270d938499bca2c522d")));
        pool_index_map_.insert(std::make_pair(229, common::Encode::HexDecode("4410f0e22159c2d0587fa9edac268bdb764deeb6")));
        pool_index_map_.insert(std::make_pair(230, common::Encode::HexDecode("2922e5839e6a0d9bb3f76ce35b4e4318183739c5")));
        pool_index_map_.insert(std::make_pair(231, common::Encode::HexDecode("0e332b72edea00dc39881ce0db59fae3952e9e62")));
        pool_index_map_.insert(std::make_pair(232, common::Encode::HexDecode("846e3fe7c4afd8b3317d0d524a35655f2dee7d3b")));
        pool_index_map_.insert(std::make_pair(233, common::Encode::HexDecode("70ff29a3199987eb73c1cf3c936d90786ef714ab")));
        pool_index_map_.insert(std::make_pair(234, common::Encode::HexDecode("51a5bade4c02f9afe68d82f4251ffcc9921fb06c")));
        pool_index_map_.insert(std::make_pair(235, common::Encode::HexDecode("026dda428a48240c3e1cfc38c4364dcb661978c4")));
        pool_index_map_.insert(std::make_pair(236, common::Encode::HexDecode("8e02528fba33d9b9998f7aa9b67a7313de4b8fc6")));
        pool_index_map_.insert(std::make_pair(237, common::Encode::HexDecode("a8272ac1a7f90a1246895f0810e07ab85ae835cb")));
        pool_index_map_.insert(std::make_pair(238, common::Encode::HexDecode("ccb9715ae46f2e17e55676723983d45d48ce8f6b")));
        pool_index_map_.insert(std::make_pair(239, common::Encode::HexDecode("1564eb65e36bf3eb6b066de36ff318d6193a29a1")));
        pool_index_map_.insert(std::make_pair(240, common::Encode::HexDecode("ddbe4a1c33d82d20ee6c2080323a044892942c74")));
        pool_index_map_.insert(std::make_pair(241, common::Encode::HexDecode("405b83d70f0979eb2d520d2dca9df396201c5c56")));
        pool_index_map_.insert(std::make_pair(242, common::Encode::HexDecode("05a62bef2656535db7d4e65f08d419255e22418a")));
        pool_index_map_.insert(std::make_pair(243, common::Encode::HexDecode("fa67fa2d75e6f57f6aa839b968500bae3ad3c26c")));
        pool_index_map_.insert(std::make_pair(244, common::Encode::HexDecode("421ab19a421d209b762ab7793ef755839afc1032")));
        pool_index_map_.insert(std::make_pair(245, common::Encode::HexDecode("e1b8b9812a329feb03bff2736a35af9ebfe30618")));
        pool_index_map_.insert(std::make_pair(246, common::Encode::HexDecode("372782e94ff6c2bd0dbfccd2a1909c22d7779db0")));
        pool_index_map_.insert(std::make_pair(247, common::Encode::HexDecode("f871ce4ba0bcc6dfd85a4be59779e754fd5ab04c")));
        pool_index_map_.insert(std::make_pair(248, common::Encode::HexDecode("6e51461e04cfff638250d67d832282ae9e767dd7")));
        pool_index_map_.insert(std::make_pair(249, common::Encode::HexDecode("9fc53884ca1946e284e321e6862795e983691222")));
        pool_index_map_.insert(std::make_pair(250, common::Encode::HexDecode("bf56e561392f49989d2078381b220436c8f03403")));
        pool_index_map_.insert(std::make_pair(251, common::Encode::HexDecode("6ed6837c1dd2f785474f7e952d4415c6cf85a06d")));
        pool_index_map_.insert(std::make_pair(252, common::Encode::HexDecode("00932c2f58ecdda87c8a9288225f9d367a09ef2f")));
        pool_index_map_.insert(std::make_pair(253, common::Encode::HexDecode("e509d43e3f3d7187c7bc84baa027a332e6f3312e")));
        pool_index_map_.insert(std::make_pair(254, common::Encode::HexDecode("e6e619f55e6c5d47aab07a4d0ebb5aff62a4a315")));
        pool_index_map_.insert(std::make_pair(255, common::Encode::HexDecode("431be10b3a0e46f8a46686c6b0c29bc743f715fa")));
    }

}

void GenesisBlockInit::GenerateRootAccounts() {
    {
        root_account_with_pool_index_map_.insert(std::make_pair(0, common::Encode::HexDecode("a8be589951f2c0a7754d5192d5b3f2de6ad4b1d0")));
        root_account_with_pool_index_map_.insert(std::make_pair(1, common::Encode::HexDecode("bf836ca8b737899fe30c8cb4c20874d91ad28a94")));
        root_account_with_pool_index_map_.insert(std::make_pair(2, common::Encode::HexDecode("ff4262f1a27017b7bb9f271d66685b415a65c2bf")));
        root_account_with_pool_index_map_.insert(std::make_pair(3, common::Encode::HexDecode("7a5df0c413de3f12b0345b40111d4b4011514a69")));
        root_account_with_pool_index_map_.insert(std::make_pair(4, common::Encode::HexDecode("566a48ef84232f2680a75ed2fbbbe46e12fc3a24")));
        root_account_with_pool_index_map_.insert(std::make_pair(5, common::Encode::HexDecode("9ddd106e1acb4182763aa873a15280ea7a991717")));
        root_account_with_pool_index_map_.insert(std::make_pair(6, common::Encode::HexDecode("51f5d9a229ca14273354c3fdc9c33746aa0c2d33")));
        root_account_with_pool_index_map_.insert(std::make_pair(7, common::Encode::HexDecode("12729ee40d6b5cfcd06e9b846d591dad156cf361")));
        root_account_with_pool_index_map_.insert(std::make_pair(8, common::Encode::HexDecode("a694bf15e45ecc3816d6c14bce3d77e1f05b9044")));
        root_account_with_pool_index_map_.insert(std::make_pair(9, common::Encode::HexDecode("c4a37b9dc4a49eb656bc6263f03bace72bfc6c4b")));
        root_account_with_pool_index_map_.insert(std::make_pair(10, common::Encode::HexDecode("9660825bff029d07bd2f5898cdfb85914a2f70cf")));
        root_account_with_pool_index_map_.insert(std::make_pair(11, common::Encode::HexDecode("8236df4c772912820864b902c0f707f3a5ba2f43")));
        root_account_with_pool_index_map_.insert(std::make_pair(12, common::Encode::HexDecode("317fd46b7b1f0e7295830da9b0343c33b92f5318")));
        root_account_with_pool_index_map_.insert(std::make_pair(13, common::Encode::HexDecode("92e0b300be43131cbeeff025ae4abccd2fab8e3a")));
        root_account_with_pool_index_map_.insert(std::make_pair(14, common::Encode::HexDecode("779e645a433e3fa0dbe3b81ed92cbf52c1a79628")));
        root_account_with_pool_index_map_.insert(std::make_pair(15, common::Encode::HexDecode("794ee3a13dc758abb6a425619c199907d9369ff5")));
        root_account_with_pool_index_map_.insert(std::make_pair(16, common::Encode::HexDecode("2e523317a410f9849063fab88a5a410c2c5d9687")));
        root_account_with_pool_index_map_.insert(std::make_pair(17, common::Encode::HexDecode("31bd38045df48dc6e82013599d165ab6d58cdfa3")));
        root_account_with_pool_index_map_.insert(std::make_pair(18, common::Encode::HexDecode("dcd11d76be2beb0364dadec1b8973cea0ac0df06")));
        root_account_with_pool_index_map_.insert(std::make_pair(19, common::Encode::HexDecode("b988000be7d219025b80160157c45674b90f626d")));
        root_account_with_pool_index_map_.insert(std::make_pair(20, common::Encode::HexDecode("b6de1ed86b01ef675a296e478b9dfb10443138a1")));
        root_account_with_pool_index_map_.insert(std::make_pair(21, common::Encode::HexDecode("b6234bfa5e7cddb84349f734e37e7d6d2b14a286")));
        root_account_with_pool_index_map_.insert(std::make_pair(22, common::Encode::HexDecode("ee999d2580857494ba6c6ad9d20e5fd5a4172ef2")));
        root_account_with_pool_index_map_.insert(std::make_pair(23, common::Encode::HexDecode("2740fa20bcb074c27c38ac024d1b935817f44985")));
        root_account_with_pool_index_map_.insert(std::make_pair(24, common::Encode::HexDecode("d55e10f5fa226c687ee956d0f9b23b8e4f91ed4b")));
        root_account_with_pool_index_map_.insert(std::make_pair(25, common::Encode::HexDecode("149c9b86eac20c148036fe2f2ab560881f9a3c7d")));
        root_account_with_pool_index_map_.insert(std::make_pair(26, common::Encode::HexDecode("1a18415544fd681ab214b477fc4341d8f0b5d953")));
        root_account_with_pool_index_map_.insert(std::make_pair(27, common::Encode::HexDecode("3c4a6ccb5868908e0b3429abd14330e9daf47690")));
        root_account_with_pool_index_map_.insert(std::make_pair(28, common::Encode::HexDecode("2cb6628799522534dbd0fbd13dd3d7aec3da436e")));
        root_account_with_pool_index_map_.insert(std::make_pair(29, common::Encode::HexDecode("4be38cd2e2825aa024edde7cf4872776e9254670")));
        root_account_with_pool_index_map_.insert(std::make_pair(30, common::Encode::HexDecode("c2b7d0a374351bfb71a00a2b4ed8051dac710e88")));
        root_account_with_pool_index_map_.insert(std::make_pair(31, common::Encode::HexDecode("074fa8a1a59c65ebcc5d1f9c3e0179703f020623")));
        root_account_with_pool_index_map_.insert(std::make_pair(32, common::Encode::HexDecode("820094484dea3a42c05674757ebaba9ec01beca1")));
        root_account_with_pool_index_map_.insert(std::make_pair(33, common::Encode::HexDecode("f9dfb383714deae9f0af08eaef7d2a9cda8f09a3")));
        root_account_with_pool_index_map_.insert(std::make_pair(34, common::Encode::HexDecode("6ab09c29dfc448383247dc58e1f542f22d2739d8")));
        root_account_with_pool_index_map_.insert(std::make_pair(35, common::Encode::HexDecode("3ac0aa4217220b143090c9c349e06e30d92d877b")));
        root_account_with_pool_index_map_.insert(std::make_pair(36, common::Encode::HexDecode("93777bfbe886dd828a8969ff812f2ba0cc5ad714")));
        root_account_with_pool_index_map_.insert(std::make_pair(37, common::Encode::HexDecode("543f90ea36c4d7e22fa6fd6584871dd305307618")));
        root_account_with_pool_index_map_.insert(std::make_pair(38, common::Encode::HexDecode("d85d9004faae8a1ee437de249d90cea81fc8b403")));
        root_account_with_pool_index_map_.insert(std::make_pair(39, common::Encode::HexDecode("2651fd9805d596d89242ec139d69d1bf2c22d4c1")));
        root_account_with_pool_index_map_.insert(std::make_pair(40, common::Encode::HexDecode("dca9739a563140efc71c0218536843b61362f4ca")));
        root_account_with_pool_index_map_.insert(std::make_pair(41, common::Encode::HexDecode("0747d5ffc1e05d7921ffe45e60892308d27243dd")));
        root_account_with_pool_index_map_.insert(std::make_pair(42, common::Encode::HexDecode("4e3091b29cbd92a582500f1d794ec5d412e25d3e")));
        root_account_with_pool_index_map_.insert(std::make_pair(43, common::Encode::HexDecode("83fef53b158c8e8ce9e567051e9b3976698f457b")));
        root_account_with_pool_index_map_.insert(std::make_pair(44, common::Encode::HexDecode("6ec7447d79c0413e24a5a05190dbd1c1aa443e97")));
        root_account_with_pool_index_map_.insert(std::make_pair(45, common::Encode::HexDecode("4ae05df2acad542dce195d20e4b9559922d411f9")));
        root_account_with_pool_index_map_.insert(std::make_pair(46, common::Encode::HexDecode("e73be15dabcfcc2f0b254be4fe46d726a299608d")));
        root_account_with_pool_index_map_.insert(std::make_pair(47, common::Encode::HexDecode("fd3f6383513a89d283da0b8b9eeeceb003d7ddd9")));
        root_account_with_pool_index_map_.insert(std::make_pair(48, common::Encode::HexDecode("495ffe934d03063594ab3d6f8f2ba64bde69c8cb")));
        root_account_with_pool_index_map_.insert(std::make_pair(49, common::Encode::HexDecode("bf850267fe58a438303790b14e1386d5742841e6")));
        root_account_with_pool_index_map_.insert(std::make_pair(50, common::Encode::HexDecode("5fb36ae898a9b4131b368f3362f46ad1248f2290")));
        root_account_with_pool_index_map_.insert(std::make_pair(51, common::Encode::HexDecode("b824784e370ecb5b7cfa5b6ba98a40bd0216d192")));
        root_account_with_pool_index_map_.insert(std::make_pair(52, common::Encode::HexDecode("5f2778a6b0f90f9c5ed3cbadbf116529b782aeb3")));
        root_account_with_pool_index_map_.insert(std::make_pair(53, common::Encode::HexDecode("3441b1da0399b87a875d35196787cfa70baed8bc")));
        root_account_with_pool_index_map_.insert(std::make_pair(54, common::Encode::HexDecode("2cdd780aaeb5c95e483b5805cd958940c43a73b9")));
        root_account_with_pool_index_map_.insert(std::make_pair(55, common::Encode::HexDecode("8df6dc84bf109461cd3fa158256dc3492de6e065")));
        root_account_with_pool_index_map_.insert(std::make_pair(56, common::Encode::HexDecode("c1e48b226a9c119aab26d89f083f2fd0a9b22d91")));
        root_account_with_pool_index_map_.insert(std::make_pair(57, common::Encode::HexDecode("86e186fea4d87ff1d7424e434c4882f362074c68")));
        root_account_with_pool_index_map_.insert(std::make_pair(58, common::Encode::HexDecode("845974cec11e8d6f8bea2f447746ed7a1741fe24")));
        root_account_with_pool_index_map_.insert(std::make_pair(59, common::Encode::HexDecode("8c6d504cf4ed908ce2625289612d7bd77948f7e0")));
        root_account_with_pool_index_map_.insert(std::make_pair(60, common::Encode::HexDecode("f9f4bf5715f3cf0f9a900564d632e9f9d45ccba9")));
        root_account_with_pool_index_map_.insert(std::make_pair(61, common::Encode::HexDecode("54d3fb5988e37adb10357d88e6e970f10e17128c")));
        root_account_with_pool_index_map_.insert(std::make_pair(62, common::Encode::HexDecode("d350492149fc30bf5c33077bf1e7e18239220dbd")));
        root_account_with_pool_index_map_.insert(std::make_pair(63, common::Encode::HexDecode("08547f0f9270e68584fae0a743f9b1c527f13473")));
        root_account_with_pool_index_map_.insert(std::make_pair(64, common::Encode::HexDecode("ae33c942cdf6f11832a6f598f2feefd79aec778c")));
        root_account_with_pool_index_map_.insert(std::make_pair(65, common::Encode::HexDecode("48a848557c4f47c630d6fad6344af03d1d933cb8")));
        root_account_with_pool_index_map_.insert(std::make_pair(66, common::Encode::HexDecode("50af8e6164d510007062c43149ec1764a10ed22a")));
        root_account_with_pool_index_map_.insert(std::make_pair(67, common::Encode::HexDecode("374f37825c1866e5c77e958c15d893caa611fad2")));
        root_account_with_pool_index_map_.insert(std::make_pair(68, common::Encode::HexDecode("1379d7b2d4a5c80634b9f3a4853c7cfc7982bce9")));
        root_account_with_pool_index_map_.insert(std::make_pair(69, common::Encode::HexDecode("08be27ae675a07f06013210802e5aabd486a041a")));
        root_account_with_pool_index_map_.insert(std::make_pair(70, common::Encode::HexDecode("4b52b3deee47dbe7eb362f7fe759800778bd7520")));
        root_account_with_pool_index_map_.insert(std::make_pair(71, common::Encode::HexDecode("60162c91c78dcab4683de629e552e28e79009b96")));
        root_account_with_pool_index_map_.insert(std::make_pair(72, common::Encode::HexDecode("021ff7eb55fe86b8c9567304edd7a529c49c166b")));
        root_account_with_pool_index_map_.insert(std::make_pair(73, common::Encode::HexDecode("a032611943b3f3eebe05cf37bacbe86f86c32a9d")));
        root_account_with_pool_index_map_.insert(std::make_pair(74, common::Encode::HexDecode("8f36921cfab72f693beebfa05867856bc6092deb")));
        root_account_with_pool_index_map_.insert(std::make_pair(75, common::Encode::HexDecode("8d826f7304b30fac69cac84a1073eb0d12e2fccc")));
        root_account_with_pool_index_map_.insert(std::make_pair(76, common::Encode::HexDecode("a5dc6dcae5a19229358584430bd599aa5bb1bfa0")));
        root_account_with_pool_index_map_.insert(std::make_pair(77, common::Encode::HexDecode("4c1b2cffc829332e6b0456238dc702c2e275ccf3")));
        root_account_with_pool_index_map_.insert(std::make_pair(78, common::Encode::HexDecode("0e5ff46da50f1f3b451c490a604929c3303a0afe")));
        root_account_with_pool_index_map_.insert(std::make_pair(79, common::Encode::HexDecode("58c967c7c91f60b2841f02371438bd3f27faa9e0")));
        root_account_with_pool_index_map_.insert(std::make_pair(80, common::Encode::HexDecode("f409fb0e0fc3ca7666d97d28b06d73283acbfa53")));
        root_account_with_pool_index_map_.insert(std::make_pair(81, common::Encode::HexDecode("1d4f7279b1a7ee931b0be718d90e25a2edb7ba78")));
        root_account_with_pool_index_map_.insert(std::make_pair(82, common::Encode::HexDecode("b1013a34a8a3bc6fda587d62d655b156d615e955")));
        root_account_with_pool_index_map_.insert(std::make_pair(83, common::Encode::HexDecode("581d13ace4522a98bffefa7e9964f75bbd85e2ed")));
        root_account_with_pool_index_map_.insert(std::make_pair(84, common::Encode::HexDecode("7235dc10ee2bcf050881d4d7d6da8783eec25bde")));
        root_account_with_pool_index_map_.insert(std::make_pair(85, common::Encode::HexDecode("982c19b34bf419733ef290e04089bcaa795c678c")));
        root_account_with_pool_index_map_.insert(std::make_pair(86, common::Encode::HexDecode("30ae5da567de2c0c6e05acafa44da56219fcbee3")));
        root_account_with_pool_index_map_.insert(std::make_pair(87, common::Encode::HexDecode("ed89a0bead2ef563ad5d445bf8aa3e79fb2638a0")));
        root_account_with_pool_index_map_.insert(std::make_pair(88, common::Encode::HexDecode("ffa566d03bc13087330071e2ed95d6a931228aec")));
        root_account_with_pool_index_map_.insert(std::make_pair(89, common::Encode::HexDecode("ff5a089ff6394575e2dc8fd748b9bb250ebac25c")));
        root_account_with_pool_index_map_.insert(std::make_pair(90, common::Encode::HexDecode("01d949319bc2810e8dc22b4fea4953c46c86437b")));
        root_account_with_pool_index_map_.insert(std::make_pair(91, common::Encode::HexDecode("07d41328ab370a4744ff088371e1fc9da11935de")));
        root_account_with_pool_index_map_.insert(std::make_pair(92, common::Encode::HexDecode("ff22f3d8209a40362d81276117eac11749a84471")));
        root_account_with_pool_index_map_.insert(std::make_pair(93, common::Encode::HexDecode("4d31a7cfe75f5f24b33b9c36364fd315e9c67bc0")));
        root_account_with_pool_index_map_.insert(std::make_pair(94, common::Encode::HexDecode("4ef8a8607f5d066df797f74c9ceb622258a9a11a")));
        root_account_with_pool_index_map_.insert(std::make_pair(95, common::Encode::HexDecode("6935942d5c40e1cc230378174c3bacc067705a07")));
        root_account_with_pool_index_map_.insert(std::make_pair(96, common::Encode::HexDecode("3b19d967d55f4f500b65638c231ecea364bdfcb2")));
        root_account_with_pool_index_map_.insert(std::make_pair(97, common::Encode::HexDecode("bfc1aedeced526601725d943d50422fa5eb45af6")));
        root_account_with_pool_index_map_.insert(std::make_pair(98, common::Encode::HexDecode("f6990367b7b25ae2418c307eb12ebc8902a3e9a9")));
        root_account_with_pool_index_map_.insert(std::make_pair(99, common::Encode::HexDecode("682b3bf645a76562ffbad35aef0fe410ed7c62d8")));
        root_account_with_pool_index_map_.insert(std::make_pair(100, common::Encode::HexDecode("84e328b21419fa0ec469961d5a03e37c6db83dab")));
        root_account_with_pool_index_map_.insert(std::make_pair(101, common::Encode::HexDecode("61f9b0d5fc10dc6ab5db875a061189b2645c7280")));
        root_account_with_pool_index_map_.insert(std::make_pair(102, common::Encode::HexDecode("bf0669477a3000e06068d6855ffa86f2c98eca20")));
        root_account_with_pool_index_map_.insert(std::make_pair(103, common::Encode::HexDecode("55eeef7e623cefc9f9a091c11fa299b44f7ed952")));
        root_account_with_pool_index_map_.insert(std::make_pair(104, common::Encode::HexDecode("e5002c4b4a9901a1a9f5255beeecb0a51404ca41")));
        root_account_with_pool_index_map_.insert(std::make_pair(105, common::Encode::HexDecode("cd5a577390adab55b98cdbf0637d97ed6b4fdc64")));
        root_account_with_pool_index_map_.insert(std::make_pair(106, common::Encode::HexDecode("a67f269dd6a73c2e61ab1147718a12d122a03c4c")));
        root_account_with_pool_index_map_.insert(std::make_pair(107, common::Encode::HexDecode("d3692f988323b1b69fecb97db4583f7c69dcb58a")));
        root_account_with_pool_index_map_.insert(std::make_pair(108, common::Encode::HexDecode("4de7edf4f382342e33838b15be0b1eceef6e4af4")));
        root_account_with_pool_index_map_.insert(std::make_pair(109, common::Encode::HexDecode("67f495407900860c99f555064c388de90bdf975e")));
        root_account_with_pool_index_map_.insert(std::make_pair(110, common::Encode::HexDecode("71e24b920554b2d0b4a427798b76c97a5187139f")));
        root_account_with_pool_index_map_.insert(std::make_pair(111, common::Encode::HexDecode("9aabe17c639841fc173a99790d780c2de34ba753")));
        root_account_with_pool_index_map_.insert(std::make_pair(112, common::Encode::HexDecode("87355a37b404340756f5cdb94740465d9d288122")));
        root_account_with_pool_index_map_.insert(std::make_pair(113, common::Encode::HexDecode("1ad3507d3ee212290e6d068816868683f5e8bde7")));
        root_account_with_pool_index_map_.insert(std::make_pair(114, common::Encode::HexDecode("55b3e93b2f59c7b23e15b72c70ed2ce3e4741cd2")));
        root_account_with_pool_index_map_.insert(std::make_pair(115, common::Encode::HexDecode("07b1aa006452425dd80f40b5b89ad0e419df8033")));
        root_account_with_pool_index_map_.insert(std::make_pair(116, common::Encode::HexDecode("0712b0fa2d3a1d300f929fce23f1de921b7afa32")));
        root_account_with_pool_index_map_.insert(std::make_pair(117, common::Encode::HexDecode("d6b663fab2f41d358c930f4e93cde61a881f1640")));
        root_account_with_pool_index_map_.insert(std::make_pair(118, common::Encode::HexDecode("1390b3177ed2f7afad9b3ca41fc9c05cb8705b40")));
        root_account_with_pool_index_map_.insert(std::make_pair(119, common::Encode::HexDecode("ff7404d15933498bdc97e74091bf30106b3eba97")));
        root_account_with_pool_index_map_.insert(std::make_pair(120, common::Encode::HexDecode("5dd4f0c824d6219620d40e57ed83ec31c3c6f421")));
        root_account_with_pool_index_map_.insert(std::make_pair(121, common::Encode::HexDecode("0b557be6b1f0914198a9309f6db8ee0e75f1290d")));
        root_account_with_pool_index_map_.insert(std::make_pair(122, common::Encode::HexDecode("ab2f33505e68156f481545c08764b89d8ca8450d")));
        root_account_with_pool_index_map_.insert(std::make_pair(123, common::Encode::HexDecode("0be02fc8e0e5ae5430d095ca7d60720544f91faf")));
        root_account_with_pool_index_map_.insert(std::make_pair(124, common::Encode::HexDecode("599c0dca2c79678d1278948d5216e4c13e457e7e")));
        root_account_with_pool_index_map_.insert(std::make_pair(125, common::Encode::HexDecode("fdb1de73d52eb5ec3c40632f3fdaf661b7ea5f22")));
        root_account_with_pool_index_map_.insert(std::make_pair(126, common::Encode::HexDecode("b356f94b0af7459d8cfa6812ca00ee7212863daf")));
        root_account_with_pool_index_map_.insert(std::make_pair(127, common::Encode::HexDecode("54381423604cf6c2b5e7966bdb0496a5d99c6123")));
        root_account_with_pool_index_map_.insert(std::make_pair(128, common::Encode::HexDecode("b135f86f70ad896cdc3fb30a8f2e2645c37c5070")));
        root_account_with_pool_index_map_.insert(std::make_pair(129, common::Encode::HexDecode("408e30df84e576503506ece74685276dbb19df22")));
        root_account_with_pool_index_map_.insert(std::make_pair(130, common::Encode::HexDecode("3d2dbb573e144ac052daa7e7cd54d0f8b9ee75ab")));
        root_account_with_pool_index_map_.insert(std::make_pair(131, common::Encode::HexDecode("1f78d713271cc6aa1a4ae1dc95d2f790bc2455c2")));
        root_account_with_pool_index_map_.insert(std::make_pair(132, common::Encode::HexDecode("699531bb317a28fad21936cc086196815a6f6313")));
        root_account_with_pool_index_map_.insert(std::make_pair(133, common::Encode::HexDecode("a67011e5d34b049fbeeab3a17565158d37fde325")));
        root_account_with_pool_index_map_.insert(std::make_pair(134, common::Encode::HexDecode("c58d35e3b10092330406914713f86c2d56bb9d57")));
        root_account_with_pool_index_map_.insert(std::make_pair(135, common::Encode::HexDecode("d96589d5bb81eff4fb17b81017f14df985a7edf2")));
        root_account_with_pool_index_map_.insert(std::make_pair(136, common::Encode::HexDecode("be5e4c7703f0bec9c7751e53643ca97fd54e4968")));
        root_account_with_pool_index_map_.insert(std::make_pair(137, common::Encode::HexDecode("09c4050ab0b8deb78a34f57538e87ba52c0183a6")));
        root_account_with_pool_index_map_.insert(std::make_pair(138, common::Encode::HexDecode("00be454600dfcd8b534c0093577bf4f57baa5c9c")));
        root_account_with_pool_index_map_.insert(std::make_pair(139, common::Encode::HexDecode("6405249e499ba0ff281b47ab8a67c881e1a2a841")));
        root_account_with_pool_index_map_.insert(std::make_pair(140, common::Encode::HexDecode("7351b456259cd41c35e78cac8b842874fcdef248")));
        root_account_with_pool_index_map_.insert(std::make_pair(141, common::Encode::HexDecode("7522c3849f4a80271d5389a261e4524eca83cced")));
        root_account_with_pool_index_map_.insert(std::make_pair(142, common::Encode::HexDecode("cd8f6279cf8d9c0dd10568e6e52818760f052442")));
        root_account_with_pool_index_map_.insert(std::make_pair(143, common::Encode::HexDecode("8f46b34880a53a7e8bfaf73904215fa1298e53bd")));
        root_account_with_pool_index_map_.insert(std::make_pair(144, common::Encode::HexDecode("f72acc691f3362d0d0524a6a7be7622fda4df5cc")));
        root_account_with_pool_index_map_.insert(std::make_pair(145, common::Encode::HexDecode("064ee3b58c2aebfc5042648e21172562833a38ed")));
        root_account_with_pool_index_map_.insert(std::make_pair(146, common::Encode::HexDecode("eeefba732256f53830900657446c2d59889b0b6b")));
        root_account_with_pool_index_map_.insert(std::make_pair(147, common::Encode::HexDecode("d17b479e4736770a9f028c4a1d2e6ef10e2ae830")));
        root_account_with_pool_index_map_.insert(std::make_pair(148, common::Encode::HexDecode("479ed2e437ed4d44dfb0b96a4377e22d99119295")));
        root_account_with_pool_index_map_.insert(std::make_pair(149, common::Encode::HexDecode("22d6ff1c8a515e42746761007d11dde5297c345c")));
        root_account_with_pool_index_map_.insert(std::make_pair(150, common::Encode::HexDecode("2f3e563d4c9a9a3d54b475103d87aca6f49ccf62")));
        root_account_with_pool_index_map_.insert(std::make_pair(151, common::Encode::HexDecode("b0d775aa859a79d79f07de4bdd1457f3a5b036cd")));
        root_account_with_pool_index_map_.insert(std::make_pair(152, common::Encode::HexDecode("8ea0203f60836b3d9274e0d17ef79975178b03fc")));
        root_account_with_pool_index_map_.insert(std::make_pair(153, common::Encode::HexDecode("44b6b1b7718776a9db9a76125e5cd9e23dd69d36")));
        root_account_with_pool_index_map_.insert(std::make_pair(154, common::Encode::HexDecode("26b893369a9345bd209520485020a65e0b290212")));
        root_account_with_pool_index_map_.insert(std::make_pair(155, common::Encode::HexDecode("f9fed792645fb35b1a08cff83e67c001a1df5303")));
        root_account_with_pool_index_map_.insert(std::make_pair(156, common::Encode::HexDecode("7f622d2aad0b52afe4bad6a50fcd55e654a5d914")));
        root_account_with_pool_index_map_.insert(std::make_pair(157, common::Encode::HexDecode("a8f821b0ea2c658dca2aa38e8b7c254e42573263")));
        root_account_with_pool_index_map_.insert(std::make_pair(158, common::Encode::HexDecode("03ed9259251d6fdd843c273ab39720bdef667696")));
        root_account_with_pool_index_map_.insert(std::make_pair(159, common::Encode::HexDecode("47469a0815d26b987b49278a8cce4e4097835ea8")));
        root_account_with_pool_index_map_.insert(std::make_pair(160, common::Encode::HexDecode("68d32cab1113030f07ae2438067f61af4316c91b")));
        root_account_with_pool_index_map_.insert(std::make_pair(161, common::Encode::HexDecode("390a109e41a32e27509e5a949c32f8b1813765f2")));
        root_account_with_pool_index_map_.insert(std::make_pair(162, common::Encode::HexDecode("afb1b446ec0ed1bae946344817fdd236fbd478cf")));
        root_account_with_pool_index_map_.insert(std::make_pair(163, common::Encode::HexDecode("c3f26dceaeaa4ad0d256060a793be7424adcb044")));
        root_account_with_pool_index_map_.insert(std::make_pair(164, common::Encode::HexDecode("e542e8b5fb037a885f3eca0fdece7154f90b9639")));
        root_account_with_pool_index_map_.insert(std::make_pair(165, common::Encode::HexDecode("6cb954aa24b360f44d0c9a68d4199541a5905ea3")));
        root_account_with_pool_index_map_.insert(std::make_pair(166, common::Encode::HexDecode("c191e486e31e5ef08c79438d86b020a94230cfe9")));
        root_account_with_pool_index_map_.insert(std::make_pair(167, common::Encode::HexDecode("8a2384e7ba4b6380aa7749b9a0a670e234e30899")));
        root_account_with_pool_index_map_.insert(std::make_pair(168, common::Encode::HexDecode("5b9f9526930761bf4da270b273528cf4dcb62ee9")));
        root_account_with_pool_index_map_.insert(std::make_pair(169, common::Encode::HexDecode("c5150185b2d799cf156a5b3ca31e8dff5fe22799")));
        root_account_with_pool_index_map_.insert(std::make_pair(170, common::Encode::HexDecode("90cfa031be3b1a6d7e79eb789a2ae2943ec10fa9")));
        root_account_with_pool_index_map_.insert(std::make_pair(171, common::Encode::HexDecode("2591aff193b3ff239a6f425e8541ae9affcea9ba")));
        root_account_with_pool_index_map_.insert(std::make_pair(172, common::Encode::HexDecode("37dca2049a9121612691fd34249f3c00fa6682e4")));
        root_account_with_pool_index_map_.insert(std::make_pair(173, common::Encode::HexDecode("dc2b4a68d690f9be1a622e88595c4b8efaa48ac2")));
        root_account_with_pool_index_map_.insert(std::make_pair(174, common::Encode::HexDecode("59702c711490488a8758a0d7e556680bae1edd6d")));
        root_account_with_pool_index_map_.insert(std::make_pair(175, common::Encode::HexDecode("c2a319462fba7c50534febc7ca5880ea228b812d")));
        root_account_with_pool_index_map_.insert(std::make_pair(176, common::Encode::HexDecode("071d3bb0a51255f3aed4acb6cd1dd48dccfbbed9")));
        root_account_with_pool_index_map_.insert(std::make_pair(177, common::Encode::HexDecode("eac6c8094b22930fc513fde16c7dcca119d93899")));
        root_account_with_pool_index_map_.insert(std::make_pair(178, common::Encode::HexDecode("90ffdcbaad1b4b57c79733866d12aad5f6578a0d")));
        root_account_with_pool_index_map_.insert(std::make_pair(179, common::Encode::HexDecode("e68c846f2715d10b15e713235cf1d64e08d46666")));
        root_account_with_pool_index_map_.insert(std::make_pair(180, common::Encode::HexDecode("47b648f8b4c1333acc66a5a2b9b741af59ce56c8")));
        root_account_with_pool_index_map_.insert(std::make_pair(181, common::Encode::HexDecode("9a072593021e5ce33ca30d5f44aa2760099d9ec7")));
        root_account_with_pool_index_map_.insert(std::make_pair(182, common::Encode::HexDecode("904838ff6d1d56177010f655c45d27f3d5189e1c")));
        root_account_with_pool_index_map_.insert(std::make_pair(183, common::Encode::HexDecode("534b9b756d27b1b5d66716e3cce86e3bcc0c11e6")));
        root_account_with_pool_index_map_.insert(std::make_pair(184, common::Encode::HexDecode("94cbf08ec00bb30972c1ed5de9db478fba71e2ec")));
        root_account_with_pool_index_map_.insert(std::make_pair(185, common::Encode::HexDecode("9c4e91a96a7591c20fe6e2c61c52d7db21d7c759")));
        root_account_with_pool_index_map_.insert(std::make_pair(186, common::Encode::HexDecode("331fd67544809111d4a6d2c8526164cfe4aec583")));
        root_account_with_pool_index_map_.insert(std::make_pair(187, common::Encode::HexDecode("9d37a75c192d7651871ca8025c02d2ef5f6fc9cd")));
        root_account_with_pool_index_map_.insert(std::make_pair(188, common::Encode::HexDecode("605d90daa1d33abe2e7b44e77d91978071b611e9")));
        root_account_with_pool_index_map_.insert(std::make_pair(189, common::Encode::HexDecode("41ee07481c4351097541de5ec958d33d11836a20")));
        root_account_with_pool_index_map_.insert(std::make_pair(190, common::Encode::HexDecode("f6a8f4563b297fac65bdd44eea04e3aa59ed9f3c")));
        root_account_with_pool_index_map_.insert(std::make_pair(191, common::Encode::HexDecode("ab815db36a044c0b750a6aad6fc8472556a5f992")));
        root_account_with_pool_index_map_.insert(std::make_pair(192, common::Encode::HexDecode("10b7023b653c9dfaca02923842bc5d3180c24db2")));
        root_account_with_pool_index_map_.insert(std::make_pair(193, common::Encode::HexDecode("edbfceef7ef655d617671ea9fa772cd6d236ff4e")));
        root_account_with_pool_index_map_.insert(std::make_pair(194, common::Encode::HexDecode("9d35795c79c2d86214603753e3f6b15fce5c1873")));
        root_account_with_pool_index_map_.insert(std::make_pair(195, common::Encode::HexDecode("01ef87ae47d6e9ef0501171199d6d90b56629716")));
        root_account_with_pool_index_map_.insert(std::make_pair(196, common::Encode::HexDecode("f89fa27211d6f8632d76ccbc2be1a8cb3d6490af")));
        root_account_with_pool_index_map_.insert(std::make_pair(197, common::Encode::HexDecode("5c6aeb20989e72c9b6e2631ff5be7837c6927e86")));
        root_account_with_pool_index_map_.insert(std::make_pair(198, common::Encode::HexDecode("0cc6a9070f2ea30a618a4e5bc80427409fd2354c")));
        root_account_with_pool_index_map_.insert(std::make_pair(199, common::Encode::HexDecode("cbcb39b95f683ebb1f1461b3beef03befe2f2f4e")));
        root_account_with_pool_index_map_.insert(std::make_pair(200, common::Encode::HexDecode("38ffff4b50fc67d0c9a32a3fce4349f6abbcfdc4")));
        root_account_with_pool_index_map_.insert(std::make_pair(201, common::Encode::HexDecode("6fbff58e8af6abdb2585cc42166b7dfcc727042b")));
        root_account_with_pool_index_map_.insert(std::make_pair(202, common::Encode::HexDecode("164ce89e638f6c6e65d51d5cf2a85e6bea8e28ad")));
        root_account_with_pool_index_map_.insert(std::make_pair(203, common::Encode::HexDecode("5e823ead2bcbd5b136a7e47d06b266e313647765")));
        root_account_with_pool_index_map_.insert(std::make_pair(204, common::Encode::HexDecode("90127b51b26081069e71c1a83b5795746bb9db13")));
        root_account_with_pool_index_map_.insert(std::make_pair(205, common::Encode::HexDecode("15da4dca3d4520322a98e0d32f427245292d217c")));
        root_account_with_pool_index_map_.insert(std::make_pair(206, common::Encode::HexDecode("87201411a33a71c387ad8e4ef4c89fc294419851")));
        root_account_with_pool_index_map_.insert(std::make_pair(207, common::Encode::HexDecode("e7e2aac2aa0da2fbb3ebc98468dea94a8dd4d622")));
        root_account_with_pool_index_map_.insert(std::make_pair(208, common::Encode::HexDecode("5407fb8532f30af3a135e394ea80325262cf4730")));
        root_account_with_pool_index_map_.insert(std::make_pair(209, common::Encode::HexDecode("ba2f1c2680e701f4562b8ed83e7fda6cee9e88be")));
        root_account_with_pool_index_map_.insert(std::make_pair(210, common::Encode::HexDecode("98f82015fdee9b5fe3390bea55fc518534dbe5c1")));
        root_account_with_pool_index_map_.insert(std::make_pair(211, common::Encode::HexDecode("371521ec8d9e89a356d412db3630d85131fd011b")));
        root_account_with_pool_index_map_.insert(std::make_pair(212, common::Encode::HexDecode("467f19a9a84394c7928c6c53e0921591f4f4ca1a")));
        root_account_with_pool_index_map_.insert(std::make_pair(213, common::Encode::HexDecode("d505f46486bf2316b0c0b073c27c0624ecf898a7")));
        root_account_with_pool_index_map_.insert(std::make_pair(214, common::Encode::HexDecode("ee665349e10a7869f96c0f7fa1c687c170896126")));
        root_account_with_pool_index_map_.insert(std::make_pair(215, common::Encode::HexDecode("0ca9bfa10901749a91c8d31ca6515965ee63a9ac")));
        root_account_with_pool_index_map_.insert(std::make_pair(216, common::Encode::HexDecode("2a0f6586a920b74473cbb1f384966741fc20ba9c")));
        root_account_with_pool_index_map_.insert(std::make_pair(217, common::Encode::HexDecode("3407fdbbe4809055066f102e68604b02d6b67e89")));
        root_account_with_pool_index_map_.insert(std::make_pair(218, common::Encode::HexDecode("b9d13e3d24c21ad860dcc5a235c5a36118973b8e")));
        root_account_with_pool_index_map_.insert(std::make_pair(219, common::Encode::HexDecode("11da971d8d169da4837a2ccf1fe13f39872f527f")));
        root_account_with_pool_index_map_.insert(std::make_pair(220, common::Encode::HexDecode("d50ede64cc89eaf4aa837411c6edcb5cee068897")));
        root_account_with_pool_index_map_.insert(std::make_pair(221, common::Encode::HexDecode("49264d567f2aaa45df6da9acfd5fddaeaab5a038")));
        root_account_with_pool_index_map_.insert(std::make_pair(222, common::Encode::HexDecode("524b6834b4bab69129587aa8c1ddfd3e7252af3a")));
        root_account_with_pool_index_map_.insert(std::make_pair(223, common::Encode::HexDecode("2839754a70a0a93eb853db788571f563caefc376")));
        root_account_with_pool_index_map_.insert(std::make_pair(224, common::Encode::HexDecode("50bc3131d46195b87fac0d5194cded860d63d5c4")));
        root_account_with_pool_index_map_.insert(std::make_pair(225, common::Encode::HexDecode("2a7d3151b79140f4d13dd1f84e8f5e02aee53c84")));
        root_account_with_pool_index_map_.insert(std::make_pair(226, common::Encode::HexDecode("30738308d66b036956016354e924d76784aca69e")));
        root_account_with_pool_index_map_.insert(std::make_pair(227, common::Encode::HexDecode("d56b79ecec2b8829cfeabeffc07c0d80f4bef25d")));
        root_account_with_pool_index_map_.insert(std::make_pair(228, common::Encode::HexDecode("e228ba72cf2aff072a147f16a081224332d330a4")));
        root_account_with_pool_index_map_.insert(std::make_pair(229, common::Encode::HexDecode("92f09e54d5b5c76cf012ede9b2f6f86318af5cd5")));
        root_account_with_pool_index_map_.insert(std::make_pair(230, common::Encode::HexDecode("3a753683a66cb6b176ef3c8e7cec73c4447e8d91")));
        root_account_with_pool_index_map_.insert(std::make_pair(231, common::Encode::HexDecode("dbd115ea1593a6724781816599326cbeadd88f66")));
        root_account_with_pool_index_map_.insert(std::make_pair(232, common::Encode::HexDecode("a88b92dda14a016512f7445cdbba1632bcb12d74")));
        root_account_with_pool_index_map_.insert(std::make_pair(233, common::Encode::HexDecode("f5cb2c51d7f8e5db225ad97a37930971221f2995")));
        root_account_with_pool_index_map_.insert(std::make_pair(234, common::Encode::HexDecode("10fb1c9f8e27dff4e12732a9a4b28864aa42f810")));
        root_account_with_pool_index_map_.insert(std::make_pair(235, common::Encode::HexDecode("8b08cfed4eb4639df73e8c42a3937f372bfbdd66")));
        root_account_with_pool_index_map_.insert(std::make_pair(236, common::Encode::HexDecode("49363597c0bf7221f3843f0ae4f6d4fe9418e5b2")));
        root_account_with_pool_index_map_.insert(std::make_pair(237, common::Encode::HexDecode("7d3ea10706e2d74c5ac359d6d75888d8bea0eda5")));
        root_account_with_pool_index_map_.insert(std::make_pair(238, common::Encode::HexDecode("145c3d10a76ec1d57f3c3cfcaa42369252312039")));
        root_account_with_pool_index_map_.insert(std::make_pair(239, common::Encode::HexDecode("a73cef10f9debffd349d727852c02a58fc62ca5c")));
        root_account_with_pool_index_map_.insert(std::make_pair(240, common::Encode::HexDecode("620f7a7942cd83532dfd914a2806f4256230b6c2")));
        root_account_with_pool_index_map_.insert(std::make_pair(241, common::Encode::HexDecode("2a66cfe7c60522ad506808e19e0f454b45c2d461")));
        root_account_with_pool_index_map_.insert(std::make_pair(242, common::Encode::HexDecode("4b280795a7eea60d2eea206785b12a459b585b0a")));
        root_account_with_pool_index_map_.insert(std::make_pair(243, common::Encode::HexDecode("623722dfc357ab61e97b5dab19f3535751ef5029")));
        root_account_with_pool_index_map_.insert(std::make_pair(244, common::Encode::HexDecode("c5c179384e8cc3537a5fe0600ffb0c74f3054646")));
        root_account_with_pool_index_map_.insert(std::make_pair(245, common::Encode::HexDecode("8d6f12dedeea09367f1d94c8d3cef00455ebc9c5")));
        root_account_with_pool_index_map_.insert(std::make_pair(246, common::Encode::HexDecode("03ed93a2ed579e9ed7770181dafd69ae3fa7168e")));
        root_account_with_pool_index_map_.insert(std::make_pair(247, common::Encode::HexDecode("9260d3ca8977d457aad3bcb8a9481afd01402117")));
        root_account_with_pool_index_map_.insert(std::make_pair(248, common::Encode::HexDecode("f05b29e84a3847f18f5b6a9b836d40356bfec224")));
        root_account_with_pool_index_map_.insert(std::make_pair(249, common::Encode::HexDecode("ac6f31eeaf8199d43e5769566e958eb5b28471a4")));
        root_account_with_pool_index_map_.insert(std::make_pair(250, common::Encode::HexDecode("20facb32947a483f0d152705433760cb32294c9d")));
        root_account_with_pool_index_map_.insert(std::make_pair(251, common::Encode::HexDecode("9938316ec5f45678393e646293733985efa4ca27")));
        root_account_with_pool_index_map_.insert(std::make_pair(252, common::Encode::HexDecode("56f05212b2c37a724283257081ab106859c17069")));
        root_account_with_pool_index_map_.insert(std::make_pair(253, common::Encode::HexDecode("10e93074da62703a39f506f2ecadf664daf5643c")));
        root_account_with_pool_index_map_.insert(std::make_pair(254, common::Encode::HexDecode("dd17e73b5eb219b4954508e64fc856f4e258c0d3")));
        root_account_with_pool_index_map_.insert(std::make_pair(255, common::Encode::HexDecode("bfd4a5f7b6a23a59aea7e161ab8421dd2fdc3801")));
    }
}

};  // namespace init

};  // namespace zjchain
