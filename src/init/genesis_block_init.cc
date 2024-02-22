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
#include "yaml-cpp/yaml.h"

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
    uint32_t count = 0;
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
#if MOCK_SIGN
    auto agg_sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
#else
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
#endif
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
    auto iter = net_pool_index_map_.find(net_id);
    if (iter != net_pool_index_map_.end()) {
        pool_acc_map = iter->second;
    } else {
        return kInitError;
    }
     
    // 每个账户分配余额，只有 shard3 中的合法账户会被分配
    uint64_t genesis_account_balance = 0;
    // if (net_id == network::kConsensusShardBeginNetworkId) {
    genesis_account_balance = common::kGenesisFoundationMaxZjc / pool_acc_map.size(); // 两个分片
    // }
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

        // if (net_id != network::kConsensusShardBeginNetworkId) {
        //     all_balance = common::kGenesisFoundationMaxZjc;
        // }
        
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
            auto pool_index_map = GetGenesisAccount(net_id);
            net_pool_index_map_.insert(std::make_pair(net_id, pool_index_map));
        }    
    }

    hasRunOnce = true;
}

const std::map<uint32_t, std::string> GenesisBlockInit::GetGenesisAccount(uint32_t net_id) {
    std::map<uint32_t, std::string> pool_index_map;
    int32_t index = -1;
    for (uint32_t i = 0; i < genesis_config_["shards"].size(); i++) {
        if (genesis_config_["shards"][i]["net_id"].as<uint32_t>() == net_id) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return pool_index_map;
    }
    
    auto shard_config = genesis_config_["shards"][index];

    for (uint32_t i = 0; i < shard_config["accounts"].size(); i++) {
        std::string account_id = shard_config["accounts"][i].as<std::string>();
        pool_index_map.insert(std::make_pair(i, common::Encode::HexDecode(account_id)));
    }
    return pool_index_map;
}

void GenesisBlockInit::GenerateRootAccounts() {
    for (uint32_t i = 0; i < genesis_config_["root"]["accounts"].size(); i++) {
        std::string account_id = genesis_config_["root"]["accounts"][i].as<std::string>();
        root_account_with_pool_index_map_.insert(std::make_pair(i, common::Encode::HexDecode(account_id)));
    }
}

};  // namespace init

};  // namespace zjchain
