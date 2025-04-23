#include "init/genesis_block_init.h"

#include <bls/bls_utils.h>
#include <cmath>
#include <consensus/hotstuff/view_block_chain.h>
#include <utility>
#include <vector>

#include <libbls/tools/utils.h>

#define private public
#define protected public
#include "common/defer.h"
#include "common/encode.h"
#include "common/global_info.h"
#include "common/random.h"
#include "common/split.h"
#include "contract/contract_manager.h"
#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_sign.h"
#include "consensus/consensus_utils.h"
// #ifndef ENABLE_HOTSTUFF
#include "consensus/zbft/zbft_utils.h"
#include "protos/zbft.pb.h"
// #endif
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "init/init_utils.h"
#include "pools/shard_statistic.h"
#include "pools/tx_pool_manager.h"
#include "protos/get_proto_hash.h"
#include "security/ecdsa/ecdsa.h"
#include "security/ecdsa/secp256k1.h"
#include "timeblock/time_block_utils.h"
#include "yaml-cpp/yaml.h"
#include "consensus/hotstuff/types.h"

namespace shardora {

namespace init {

GenesisBlockInit::GenesisBlockInit(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<db::Db>& db)
        : account_mgr_(account_mgr), block_mgr_(block_mgr), db_(db) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    bls_pk_json_ = nlohmann::json::array();
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
        CreatePoolsAddressInfo(network::kRootCongressNetworkId);
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
        PrepareCreateGenesisBlocks(network::kRootCongressNetworkId);
        res = CreateRootGenesisBlocks(real_root_genesis_nodes,
                                      real_cons_genesis_nodes_of_shards,
                                      genesis_acount_balance_map);
        for (uint32_t i = 0; i < real_root_genesis_nodes.size(); ++i) {
            prikeys.push_back(real_root_genesis_nodes[i]->prikey);
        }

#ifndef DISABLE_GENESIS_BLS_VERIFY
        // 验证部分私钥并保存多项式承诺，如果不需要轮换可以注释掉，大幅度节约创世块计算时间和部分空间
        ComputeG2sForNodes(prikeys);
#endif
        // SaveGenisisPoolHeights(network::kRootCongressNetworkId);
    } else { // 构建某 shard 创世网络
        // TODO 这种写法是每个 shard 单独的 shell 命令，不适用，需要改
        for (uint32_t i = 0; i < real_cons_genesis_nodes_of_shards.size(); i++) {
            std::vector<std::string> prikeys;
            uint32_t shard_node_net_id = i + network::kConsensusShardBeginNetworkId;
            std::vector<GenisisNodeInfoPtr> cons_genesis_nodes = real_cons_genesis_nodes_of_shards[i];

            if (shard_node_net_id == 0 || cons_genesis_nodes.size() == 0) {
                continue;
            }

            CreatePoolsAddressInfo(shard_node_net_id);
            CreateNodePrivateInfo(shard_node_net_id, 1llu, cons_genesis_nodes);
            common::GlobalInfo::Instance()->set_network_id(shard_node_net_id);
            PrepareCreateGenesisBlocks(shard_node_net_id);            
            res = CreateShardGenesisBlocks(real_root_genesis_nodes,
                                           cons_genesis_nodes,
                                           shard_node_net_id,
                                           genesis_acount_balance_map); // root 节点账户创建在第一个 shard 网络
            assert(res == kInitSuccess);

            for (uint32_t i = 0; i < cons_genesis_nodes.size(); ++i) {
                prikeys.push_back(cons_genesis_nodes[i]->prikey);
            }

#ifndef DISABLE_GENESIS_BLS_VERIFY            
            ComputeG2sForNodes(prikeys);
#endif
            // SaveGenisisPoolHeights(shard_node_net_id);
        }
    }

    // db_->CompactRange("", "");
    if (net_type == GenisisNetworkType::RootNetwork) {
        FILE* fd = fopen("./bls_pk", "w");
        auto str = bls_pk_json_.dump();
        auto w_size = fwrite(str.c_str(), 1, str.size(), fd);
        fclose(fd);
        if (w_size != str.size()) {
            return kInitError;
        }
    }

    PrintGenisisAccounts();
    return res;
}

// 网络中每个 pool 都有个 address
void GenesisBlockInit::CreatePoolsAddressInfo(uint16_t network_id) {
    immutable_pool_address_info_ = std::make_shared<address::protobuf::AddressInfo>();
    std::string immutable_pool_addr;
    immutable_pool_addr.reserve(security::kUnicastAddressLength);
    immutable_pool_addr.append(common::kRootPoolsAddressPrefix);
    immutable_pool_addr.append(std::string((char*)&network_id, sizeof(network_id)));
    immutable_pool_address_info_->set_pubkey("");
    immutable_pool_address_info_->set_balance(0);
    immutable_pool_address_info_->set_sharding_id(network_id);
    immutable_pool_address_info_->set_pool_index(common::kImmutablePoolSize);
    immutable_pool_address_info_->set_addr(immutable_pool_addr);
    immutable_pool_address_info_->set_type(address::protobuf::kImmutablePoolAddress);
    immutable_pool_address_info_->set_latest_height(0);
    immutable_pool_address_info_->set_nonce(0);
    ZJC_DEBUG("init pool immutable index net: %u, base address: %s", 
        network_id, common::Encode::HexEncode(immutable_pool_addr).c_str());
    uint32_t i = 0;
    std::unordered_set<uint32_t> pool_idx_set;
    for (uint32_t i = 0; i < common::kInvalidUint32; ++i) {
        auto hash = common::Hash::keccak256(std::to_string(i) + std::to_string(network_id));
        auto addr = hash.substr(
            hash.size() - security::kUnicastAddressLength, 
            security::kUnicastAddressLength);
        auto pool_idx = common::GetAddressPoolIndex(addr);
        if (pool_idx_set.size() >= common::kImmutablePoolSize) {
            break;
        }

        auto iter = pool_idx_set.find(pool_idx);
        if (iter != pool_idx_set.end()) {
            continue;
        }

        pool_address_info_[pool_idx] = std::make_shared<address::protobuf::AddressInfo>();
        pool_address_info_[pool_idx]->set_pubkey("");
        pool_address_info_[pool_idx]->set_balance(0);
        pool_address_info_[pool_idx]->set_sharding_id(network_id);
        pool_address_info_[pool_idx]->set_pool_index(pool_idx);
        pool_address_info_[pool_idx]->set_addr(addr);
        pool_address_info_[pool_idx]->set_type(address::protobuf::kPoolAddress);
        pool_address_info_[pool_idx]->set_latest_height(0);
        pool_address_info_[pool_idx]->set_nonce(0);
        pool_idx_set.insert(pool_idx);
        ZJC_DEBUG("init pool index: %u, base address: %s", 
            pool_idx, common::Encode::HexEncode(addr).c_str());
    }
}

void ComputeG2ForNode(
        const std::string& prikey,
        uint32_t k,
        const std::shared_ptr<protos::PrefixDb>& prefix_db,
        const std::vector<std::string>& prikeys) {
    std::cout << "Start ComputeG2ForNode k: " << k << " n: " << prikeys.size() << std::endl;
    std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
    secptr->SetPrivateKey(prikey);
    bls::protobuf::LocalPolynomial local_poly;
    std::vector<libff::alt_bn128_Fr> polynomial;
    prefix_db->SaveLocalElectPos(secptr->GetAddress(), k);
    if (prefix_db->GetLocalPolynomial(secptr, secptr->GetAddress(), &local_poly)) {
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
            if (!prefix_db->GetVerifiedG2s(
                        mem_idx,
                        secptr->GetAddress(),
                        valid_t,
                        &verfy_final_vals)) {
                if (!CheckRecomputeG2s(mem_idx, valid_t, secptr->GetAddress(), prefix_db, verfy_final_vals)) {
                    assert(false);
                    continue;
                }
            }

            bls::protobuf::JoinElectInfo join_info;
            if (!prefix_db->GetNodeVerificationVector(secptr->GetAddress(), &join_info)) {
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
    std::cout << "End ComputeG2ForNode k: " << k << " n: " << prikeys.size() << std::endl;
}

void GenesisBlockInit::ComputeG2sForNodes(const std::vector<std::string>& prikeys) {
    std::vector<std::thread> threads;
    for (uint32_t k = 0; k < prikeys.size(); ++k) {
        threads.push_back(std::thread(ComputeG2ForNode, prikeys[k], k, prefix_db_, prikeys));
        if (threads.size() >= 8 || k == prikeys.size() - 1) {
            for (uint32_t i = 0; i < threads.size(); ++i) {
                threads[i].join();
            }

            threads.clear();
        }        
    }

    db_->ClearPrefix("db_for_gid_");
}

void GenesisBlockInit::PrepareCreateGenesisBlocks(uint32_t shard_node_net_id) {
    std::shared_ptr<security::Security> security = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync = nullptr;
    // 初始化本节点所有的 tx pool 和 cross tx pool
    pools_mgr_ = std::make_shared<pools::TxPoolManager>(security, db_, kv_sync, account_mgr_);
    // SaveGenisisPoolHeights(shard_node_net_id);
    std::shared_ptr<pools::ShardStatistic> statistic_mgr = nullptr;
    std::shared_ptr<contract::ContractManager> ct_mgr = nullptr;
    account_mgr_->Init(db_, pools_mgr_);
    block_mgr_->Init(account_mgr_, db_, pools_mgr_, statistic_mgr, security, ct_mgr, nullptr, "", nullptr);
};

bool CheckRecomputeG2s(
        uint32_t local_member_index,
        uint32_t valid_t,
        const std::string& id,
        const std::shared_ptr<protos::PrefixDb>& prefix_db,
        bls::protobuf::JoinElectBlsInfo& verfy_final_vals) {
    assert(valid_t > 1);
    bls::protobuf::JoinElectInfo join_info;
    if (!prefix_db->GetNodeVerificationVector(id, &join_info)) {
        return false;
    }

    int32_t min_idx = 0;
    if (join_info.g2_req().verify_vec_size() >= 32) {
        min_idx = join_info.g2_req().verify_vec_size() - 32;
    }

    libff::alt_bn128_G2 verify_g2s = libff::alt_bn128_G2::zero();
    int32_t begin_idx = valid_t - 1;
    for (; begin_idx > min_idx; --begin_idx) {
        if (prefix_db->GetVerifiedG2s(local_member_index, id, begin_idx + 1, &verfy_final_vals)) {
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
        prefix_db->SaveVerifiedG2s(local_member_index, id, i + 1, verfy_final_vals);
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
    auto callback = [&](uint32_t idx) -> void {
        std::string file = std::string("./") + common::Encode::HexEncode(genesis_nodes[idx]->id);
        bool file_valid = true;
        bls::protobuf::LocalPolynomial local_poly;
        FILE* fd = fopen(file.c_str(), "r");
        if (fd != nullptr) {
            fseek(fd, 0, SEEK_END);
            long file_size = ftell(fd);
            fseek(fd, 0, SEEK_SET);
            
            char* data = new char[file_size+1];
            if (fgets(data, file_size+1, fd) == nullptr) {
                ZJC_FATAL("load bls init info failed: %s", file.c_str());
                return;
            }

            fclose(fd);
            std::string tmp_data(data, strlen(data) - 1);
            std::string val = common::Encode::HexDecode(tmp_data);
            if (!local_poly.ParseFromString(val)) {
                ZJC_FATAL("load bls init info failed!");
                return;
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
        bls::protobuf::JoinElectInfo& join_info = genesis_nodes[idx]->g2_val;
        join_info.set_member_idx(idx);
        join_info.set_shard_id(sharding_id);
        join_info.set_addr(genesis_nodes[idx]->id);
        auto* req = join_info.mutable_g2_req();
        auto g2_vec = dkg_instance.VerificationVector(genesis_nodes[idx]->polynomial);
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
        }

        prefix_db_->SaveLocalPolynomial(secptr, secptr->GetAddress(), local_poly);
        prefix_db_->AddBlsVerifyG2(secptr->GetAddress(), *req);
    };

    std::vector<std::thread> thread_vec;
    for (uint32_t idx = 0; idx < genesis_nodes.size(); ++idx) {
        thread_vec.emplace_back(callback, idx);
        if (thread_vec.size() >= 8 || idx == genesis_nodes.size() - 1) {
            for (uint32_t i = 0; i < thread_vec.size(); ++i) {
                thread_vec[i].join();
            }

            thread_vec.clear();
        }
        // callback(idx);
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
        ZJC_DEBUG("save network %u, index: %d, prikey: %s, bls prikey: %s, enc: %s",
            sharding_id, idx, 
            common::Encode::HexEncode(genesis_nodes[idx]->prikey).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(genesis_nodes[idx]->bls_prikey).c_str(),
            common::Encode::HexEncode(enc_data).c_str());
    }

    common_pk_[sharding_id] = common_public_key;
    ZJC_DEBUG("success create common pk: %u, %s",
        sharding_id, libBLS::ThresholdUtils::fieldElementToString(common_public_key.X.c0).c_str());
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
        block::protobuf::Block& block) {
    view_block::protobuf::ViewBlockItem view_block_item;
    auto res = prefix_db_->GetBlockWithHeight(
        network::kRootCongressNetworkId,
        elect_block.shard_network_id() % common::kImmutablePoolSize,
        elect_block.prev_members().prev_elect_height(),
        &view_block_item);
    if (!res) {
        ELECT_ERROR("get prev block error[%d][%d][%lu].",
            network::kRootCongressNetworkId,
            common::kImmutablePoolSize,
            elect_block.prev_members().prev_elect_height());
        return;
    }

    auto& block_item = view_block_item.block_info();
    if (block_item.tx_list_size() != 1) {
        ELECT_ERROR("not has tx list size: %d", block_item.tx_list_size());
        assert(false);
        return;
    }

    *block.mutable_prev_elect_block() = block_item.elect_block();
}

int GenesisBlockInit::CreateElectBlock(
        uint32_t shard_netid, // 要被选举的 shard 网络
        std::string& root_pre_hash,
        std::string& root_pre_vb_hash,
        uint64_t height,
        uint64_t prev_height,
        hotstuff::View view,
        FILE* root_gens_init_block_file,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes) {
    if (genesis_nodes.size() == 0) {
        return kInitSuccess;
    }
    
    auto account_info = immutable_pool_address_info_;
    auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
    auto* tenon_block = view_block_ptr->mutable_block_info();
    auto tx_list = tenon_block->mutable_tx_list();
    auto tx_info = tx_list->Add();
    tx_info->set_step(pools::protobuf::kConsensusRootElectShard);
    tx_info->set_from("");
    tx_info->set_to(account_info->addr());
    tx_info->set_nonce(account_info->nonce());
    account_info->set_nonce(account_info->nonce() + 1);
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
        // agg_bls
        auto agg_bls_pk_proto = bls::BlsPublicKey2Proto((*iter)->agg_bls_pk);
        if (agg_bls_pk_proto) {
            in->mutable_agg_bls_pk()->CopyFrom(*agg_bls_pk_proto);
        }
        auto proof_proto = bls::BlsPopProof2Proto((*iter)->agg_bls_pk_proof);
        if (proof_proto) {
            in->mutable_agg_bls_pk_proof()->CopyFrom(*proof_proto);
        }

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
        ZJC_WARN("genesis elect shard: %u, prev_height: %lu, "
            "init bls common public key: %s, %s, %s, %s", 
            shard_netid, prev_height, 
            common_pk_strs->at(0).c_str(), 
            common_pk_strs->at(1).c_str(), 
            common_pk_strs->at(2).c_str(), 
            common_pk_strs->at(3).c_str());
        nlohmann::json item;
        item["n"] = genesis_nodes.size();
        item["shard_id"] = shard_netid;
        item["prev_height"] = prev_height;
        item["x_c0"] = common_pk_strs->at(0);
        item["x_c1"] = common_pk_strs->at(1);
        item["y_c0"] = common_pk_strs->at(2);
        item["y_c1"] = common_pk_strs->at(3);
        bls_pk_json_.push_back(item);
        SetPrevElectInfo(ec_block, *tenon_block);
    }

    *tenon_block->mutable_elect_block() = ec_block;
    tenon_block->set_version(common::kTransactionVersion);
    // 这个 pool index 用了 shard 的值而已
    view_block_ptr->set_parent_hash(root_pre_vb_hash);
    if (CreateAllQc(
            network::kRootCongressNetworkId,
            shard_netid,
            view, 
            root_genesis_nodes, 
            view_block_ptr) != kInitSuccess) {
        assert(false);
        return kInitError;
    }
    
    auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
    auto& db_batch = *db_batch_ptr;
    auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
    ZJC_DEBUG("success save latest elect block: %u, %lu", ec_block.shard_network_id(), ec_block.elect_height());
    std::string ec_val = common::Encode::HexEncode(view_block_ptr->SerializeAsString()) +
        "-" + common::Encode::HexEncode(ec_block.SerializeAsString()) + "\n";    
    fputs(ec_val.c_str(), root_gens_init_block_file);
    AddBlockItemToCache(view_block_ptr, db_batch);
    block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block_ptr, db_batch);
    block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
    root_pre_hash = hotstuff::GetQCMsgHash(view_block_ptr->qc());
    root_pre_vb_hash = view_block_ptr->qc().view_block_hash();
    db_->Put(db_batch);
    auto account_ptr = account_mgr_->GetAcountInfoFromDb(account_info->addr());
    if (account_ptr == nullptr) {
        ZJC_FATAL("get address failed! [%s]",
            common::Encode::HexEncode(account_info->addr()).c_str());
    }

    if (account_ptr->balance() != 0) {
        ZJC_FATAL("get address balance failed! [%s]",
            common::Encode::HexEncode(account_info->addr()).c_str());
    }

    return kInitSuccess;
}

int GenesisBlockInit::CreateAllQc(
        uint32_t  network_id,
        uint32_t  pool_index,
        uint64_t view,
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes, 
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
    auto* commit_qc = view_block_ptr->mutable_qc();
    commit_qc->set_network_id(network_id);
    commit_qc->set_pool_index(pool_index);
    commit_qc->set_view(view);
    commit_qc->set_leader_idx(0);
    commit_qc->set_elect_height(1);
    auto view_block_hash = hotstuff::GetBlockHash(*view_block_ptr);
    commit_qc->set_view_block_hash(view_block_hash);
    std::shared_ptr<libff::alt_bn128_G1> agg_sign;
    BlsAggSignViewBlock(genesis_nodes, *commit_qc, agg_sign);
    if (!agg_sign) {
        assert(false);
        return kInitError;
    }

    commit_qc->set_sign_x(libBLS::ThresholdUtils::fieldElementToString(agg_sign->X));
    commit_qc->set_sign_y(libBLS::ThresholdUtils::fieldElementToString(agg_sign->Y));
    ZJC_DEBUG("success create qc: %u_%u_%lu, agg sign x: %s",
        network_id, pool_index, view_block_ptr->qc().view(), commit_qc->sign_x().c_str());
    assert(!view_block_ptr->qc().sign_x().empty());
    return kInitSuccess;
}

int GenesisBlockInit::GenerateRootSingleBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        FILE* root_gens_init_block_file,
        uint64_t* root_pool_height,
        hotstuff::View* root_pool_view) {
    if (root_gens_init_block_file == nullptr) {
        return kInitError;
    }

    // for root single block chain
    // 呃，这个账户不是已经创建了么
    std::string root_pre_hash;
    std::string root_pre_vb_hash;
    {
        // 创建一个块用于创建root_pool_addr账户
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        auto tx_list = tenon_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_from("");
        tx_info->set_to(immutable_pool_address_info_->addr());
        tx_info->set_amount(0);
        tx_info->set_balance(0);
        tx_info->set_gas_limit(0);
        tx_info->set_nonce(immutable_pool_address_info_->nonce());
        immutable_pool_address_info_->set_nonce(immutable_pool_address_info_->nonce() + 1);
        tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_height(root_pool_height[common::kImmutablePoolSize]++);
        // TODO 此处 network_id 一定是 root
        view_block_ptr->set_parent_hash(root_pre_vb_hash);
        if (CreateAllQc(
                common::GlobalInfo::Instance()->network_id(),
                common::kImmutablePoolSize,
                root_pool_view[common::kImmutablePoolSize]++, 
                genesis_nodes, 
                view_block_ptr) != kInitSuccess) {
            assert(false);
            return kInitError;
        }
        
        fputs((common::Encode::HexEncode(view_block_ptr->SerializeAsString()) + "\n").c_str(),
            root_gens_init_block_file);
        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
        AddBlockItemToCache(view_block_ptr, db_batch);
        block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block_ptr, db_batch);
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        std::string pool_hash;
        uint64_t pool_height = 0;
        uint64_t tm_height;
        uint64_t tm_with_block_height;
        root_pre_hash = hotstuff::GetQCMsgHash(view_block_ptr->qc());
        root_pre_vb_hash = view_block_ptr->qc().view_block_hash();
        db_->Put(db_batch);
    }

    {
        // 创建时间块
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        auto tx_list = tenon_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_from("");
        tx_info->set_to(immutable_pool_address_info_->addr());
        tx_info->set_nonce(immutable_pool_address_info_->nonce());
        immutable_pool_address_info_->set_nonce(immutable_pool_address_info_->nonce() + 1);
        tx_info->set_amount(0);
        tx_info->set_balance(0);
        tx_info->set_gas_limit(0);
        tx_info->set_step(pools::protobuf::kConsensusRootTimeBlock);
        tx_info->set_gas_limit(0llu);
        tx_info->set_amount(0);
        
        tenon_block->set_height(root_pool_height[common::kImmutablePoolSize]++);
        timeblock::protobuf::TimeBlock& tm_block = *tenon_block->mutable_timer_block();
        tm_block.set_timestamp(common::TimeUtils::TimestampSeconds());
        tm_block.set_height(tenon_block->height());
        tm_block.set_vss_random(common::Random::RandomUint64());
        tenon_block->set_version(common::kTransactionVersion);
        // TODO network_id 一定是 root
        view_block_ptr->set_parent_hash(root_pre_vb_hash);
        if (CreateAllQc(
                common::GlobalInfo::Instance()->network_id(),
                common::kImmutablePoolSize,
                root_pool_view[common::kImmutablePoolSize]++, 
                genesis_nodes, 
                view_block_ptr) != kInitSuccess) {
            assert(false);
            return kInitError;
        }
        auto tmp_str = view_block_ptr->SerializeAsString();
        
        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
        fputs((common::Encode::HexEncode(tmp_str) + "\n").c_str(), root_gens_init_block_file);
//         tmblock::TimeBlockManager::Instance()->UpdateTimeBlock(1, now_tm, now_tm);
        AddBlockItemToCache(view_block_ptr, db_batch);
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block_ptr, db_batch);
        std::string pool_hash;
        uint64_t pool_height = 0;
        uint64_t tm_height;
        uint64_t tm_with_block_height;
        root_pre_hash = hotstuff::GetQCMsgHash(view_block_ptr->qc());
        root_pre_vb_hash = view_block_ptr->qc().view_block_hash();
        db_->Put(db_batch);
    }

    return kInitSuccess;
}

int GenesisBlockInit::GenerateShardSingleBlock(uint32_t sharding_id) {
    FILE* root_gens_init_block_file = fopen("./root_blocks", "r");
    if (root_gens_init_block_file == nullptr) {
        return kInitError;
    }

    fseek(root_gens_init_block_file, 0, SEEK_END);
    long file_size = ftell(root_gens_init_block_file);
    fseek(root_gens_init_block_file, 0, SEEK_SET);

    std::cout << "root_blocks size: " << file_size << std::endl;

    char* data = new char[file_size + 1];
    defer({
        delete[] data;
    });
    
    uint32_t block_count = 0;
    auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
    auto& db_batch = *db_batch_ptr;
    while (fgets(data, file_size + 1, root_gens_init_block_file) != nullptr) {
        // root_gens_init_block_file 中保存的是 root pool 账户 block，和时间快 block，同步过来
        // auto tenon_block = std::make_shared<block::protobuf::Block>();
        std::string tmp_data(data, strlen(data) - 1);
        common::Split<> tmp_split(tmp_data.c_str(), '-', tmp_data.size());
        std::string block_str = tmp_data;
        std::string ec_block_str;
        if (tmp_split.Count() == 2) {
            block_str = tmp_split[0];
            ec_block_str = common::Encode::HexDecode(tmp_split[1]);
        }

        auto pb_v_block = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto str = common::Encode::HexDecode(block_str);
        if (!pb_v_block->ParseFromString(str)) {
            assert(false);
            return kInitError;
        }

        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(pb_v_block->block_info());
        AddBlockItemToCache(pb_v_block, db_batch);
        // 同步 root_gens_init_block_file 中 block 中的账户和 block
        // 无非就是各节点账户，上文中已经加过了，这里不好区分 root_blocks 不同 shard 的账户
        // block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block, db_batch);

        // 选举块、时间块无论 shard 都是要全网同步的
        block_mgr_->GenesisNewBlock(pb_v_block, db_batch);
    }
    fclose(root_gens_init_block_file);
    // flush 磁盘
    db_->Put(db_batch);
    {
        auto addr_info = immutable_pool_address_info_;
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

// CreateRootGenesisBlocks 为 root 网络的每个 pool 生成创世块
int GenesisBlockInit::CreateRootGenesisBlocks(
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map) {
    // 256 个 root 创世账号
    // GenerateRootAccounts();
    // 256 x shard_num 个 shard 创世账号
    InitShardGenesisAccount();
    uint64_t genesis_account_balance = 0llu;
    uint64_t all_balance = 0llu;
    std::unordered_map<uint32_t, std::string> pool_prev_hash_map;
    std::unordered_map<uint32_t, hotstuff::HashStr> pool_prev_vb_hash_map;
    std::string prehashes[common::kImmutablePoolSize]; // 256
    std::string vb_prehashes[common::kImmutablePoolSize] = {""};
    // view 从 0 开始
    hotstuff::View vb_latest_view[common::kImmutablePoolSize+1] = {0};
    // 为创世账户在 root 网络中创建创世块
    // 创世块中包含：创建初始账户，以及节点选举类型的交易
    uint32_t address_count_now = 0;
    // 给每个账户在 net_id 网络中创建块，并分配到不同的 pool 当中
    FILE* root_gens_init_block_file = fopen("./root_blocks", "w");
    uint64_t pool_with_heights[common::kInvalidPoolIndex] = { 0llu };
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        std::string address = common::Encode::HexDecode("0000000000000000000000000000000000000000");
        while (true) {
            auto private_key = common::Random::RandomString(32);
            security::Ecdsa ecdsa;
            ecdsa.SetPrivateKey(private_key);
            address = ecdsa.GetAddress();
            if (common::GetAddressPoolIndex(address) == i) {
                break;
            }
        }

        std::map<block::protobuf::BlockTx*, uint32_t> tx2net_map_for_account; 
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        auto tx_list = tenon_block->mutable_tx_list();
        {
            auto tx_info = tx_list->Add();
            tx_info->set_from("");
            // 每个 root 账户地址都对应一个 pool 账户，先把创世账户中涉及到的 pool 账户创建出来
            tx_info->set_to(address);
            tx_info->set_nonce(0);
            tx_info->set_amount(genesis_account_balance);
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
            // root 网络的 pool addr 账户创建在 shard3?
            tx2net_map_for_account.insert(std::make_pair(tx_info, network::kConsensusShardBeginNetworkId));
        }
       
        // 创建 root 创世账户，貌似没什么用
        {
            auto tx_info = tx_list->Add();
            tx_info->set_to(pool_address_info_[i]->addr());
            tx_info->set_nonce(pool_address_info_[i]->nonce());
            pool_address_info_[i]->set_nonce(pool_address_info_[i]->nonce() + 1);
            tx_info->set_from("");
            tx_info->set_amount(genesis_account_balance); // 余额 0 即可
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
            // root 创世账户也创建在 shard3?
            tx2net_map_for_account.insert(std::make_pair(tx_info, network::kConsensusShardBeginNetworkId));
        }

        for (auto shard_iter = net_pool_index_map_.begin(); shard_iter != net_pool_index_map_.end(); ++shard_iter) {
            auto& pool_map = shard_iter->second;
            uint32_t net_id = shard_iter->first;
            auto pool_iter = pool_map.find(i);
            if (pool_iter != pool_map.end()) {
                for (auto addr_iter = pool_iter->second.begin(); addr_iter != pool_iter->second.end(); ++addr_iter) {
                    // 向 shard 账户转账，root 网络中的账户余额不重要，主要是记录下此 block 的 shard 信息即可
                    auto tx_info = tx_list->Add();
                    tx_info->set_nonce(addr_iter->second++);
                    tx_info->set_from("");
                    tx_info->set_to(addr_iter->first);
                    tx_info->set_amount(genesis_account_balance);
                    tx_info->set_balance(genesis_account_balance);
                    tx_info->set_gas_limit(0);
                    tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
                    // shard 创世账户创建在对应的 net
                    tx2net_map_for_account.insert(std::make_pair(tx_info, net_id));
                }
            }
        }

        for (uint32_t member_idx = 0; member_idx < root_genesis_nodes.size(); ++member_idx) {
            // 将 root 节点的选举交易打包到对应的 pool 块中
            if (common::GetAddressPoolIndex(root_genesis_nodes[member_idx]->id) == i) {
                auto join_elect_tx_info = tx_list->Add();
                join_elect_tx_info->set_step(pools::protobuf::kJoinElect);
                join_elect_tx_info->set_from(root_genesis_nodes[member_idx]->id);
                join_elect_tx_info->set_to("");
                join_elect_tx_info->set_nonce(root_genesis_nodes[member_idx]->nonce++);
                join_elect_tx_info->set_amount(0);
                join_elect_tx_info->set_gas_limit(0);
                join_elect_tx_info->set_gas_used(0);
                join_elect_tx_info->set_balance(0);
                join_elect_tx_info->set_status(0);
                bls::protobuf::JoinElectInfo* join_info = tenon_block->add_joins();
                *join_info = root_genesis_nodes[member_idx]->g2_val;
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
                    join_elect_tx_info->set_nonce(cons_genesis_nodes[member_idx]->nonce++);
                    join_elect_tx_info->set_amount(0);
                    join_elect_tx_info->set_gas_limit(0);
                    join_elect_tx_info->set_gas_used(0);
                    join_elect_tx_info->set_balance(0);
                    join_elect_tx_info->set_status(0);
                    bls::protobuf::JoinElectInfo* join_info = tenon_block->add_joins();
                    *join_info = cons_genesis_nodes[member_idx]->g2_val;
                    // 选举交易涉及账户分配到对应 shard
                    tx2net_map_for_account.insert(std::make_pair(join_elect_tx_info, net_id));
                }
            }
        }

        tenon_block->set_version(common::kTransactionVersion);
        // 为此 shard 的此 pool 打包一个块，这个块中有某些创世账户的生成交易，有某些root和shard节点的选举交易
        tenon_block->set_height(pool_with_heights[i]++);
        tenon_block->set_timeblock_height(0);
        // 块所属的 network 自然是要创建的网络，这个函数是 root 网络，network_id 自然是 root
        // 所有 root 节点对块进行签名
        auto hash = hotstuff::GetQCMsgHash(view_block_ptr->qc());
        prehashes[i] = hash;
        // 更新对应 pool 当前最新块的 hash 值
        pool_prev_hash_map[i] = hash;
        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;

        view_block_ptr->set_parent_hash(vb_prehashes[i]);
        if (CreateAllQc(
                network::kRootCongressNetworkId,
                i,
                vb_latest_view[i]++, 
                root_genesis_nodes, 
                view_block_ptr) != kInitSuccess) {
            assert(false);
            return kInitError;
        }

        pool_prev_vb_hash_map[i] = view_block_ptr->qc().view_block_hash();
        vb_prehashes[i] = view_block_ptr->qc().view_block_hash();
        
        // 提交 view block
        // 更新交易池最新信息
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(view_block_ptr->block_info());
        fputs(
            (common::Encode::HexEncode(view_block_ptr->SerializeAsString()) + "\n").c_str(), 
            root_gens_init_block_file);
        AddBlockItemToCache(view_block_ptr, db_batch);
        // 持久化块中涉及的庄户信息，统一创建块当中的账户们到 shard 3
        // 包括 root 创世账户，shard 创世账户，root 和 shard 节点账户

        // 这里将 block 中涉及的账户信息，在不同的 network 中创建
        // 其实和 CraeteShartGenesisBlocks 中对于 shard 创世账户的持久化部分重复了，但由于是 kv 所以没有影响
        for (auto it = tx2net_map_for_account.begin(); it != tx2net_map_for_account.end(); ++it) {
            auto tx = it->first;
            uint32_t net_id = it->second;
            
            block_mgr_->GenesisAddOneAccount(net_id, *tx, tenon_block->height(), db_batch);
        }
        // 出块，并处理块中不同类型的交易
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        // 处理选举交易（??? 这里没有和 GenesisNewBlock 重复吗）
        // TODO 感觉重复，可实验
        // for (uint32_t i = 0; i < root_genesis_nodes.size(); ++i) {
        //     for (int32_t tx_idx = 0; tx_idx < tenon_block->tx_list_size(); ++tx_idx) {
        //         if (tenon_block->tx_list(tx_idx).step() == pools::protobuf::kJoinElect) {
        //             block_mgr_->HandleJoinElectTx(*view_block_ptr, tenon_block->tx_list(tx_idx), db_batch);
        //         }
        //     }
        // }

        db_->Put(db_batch);
         // 获取该 pool 对应的 root 账户，做一些余额校验，这里 root 账户中余额其实是 0
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            ZJC_FATAL("get address info failed! [%s]",
                common::Encode::HexEncode(address).c_str());
        }

        if (account_ptr->balance() != genesis_account_balance) {
            ZJC_FATAL("get address balance failed! [%s]",
                common::Encode::HexEncode(address).c_str());
        }

        all_balance += account_ptr->balance();
        ZJC_INFO("new address %s, genesis balance: %lu, nonce: %lu",
            common::Encode::HexEncode(account_ptr->addr()).c_str(), 
            account_ptr->balance(),
            account_ptr->nonce());

    }

    // 选举 root leader，选举 shard leader
    // 每次 ElectBlock 出块会生效前一个选举块
    if (CreateElectBlock(
            network::kRootCongressNetworkId,
            prehashes[network::kRootCongressNetworkId],
            vb_prehashes[network::kRootCongressNetworkId],
            pool_with_heights[common::kImmutablePoolSize]++,
            common::kInvalidUint64,
            vb_latest_view[network::kRootCongressNetworkId]++,
            root_gens_init_block_file,
            root_genesis_nodes,
            root_genesis_nodes) != kInitSuccess) {
        ZJC_FATAL("CreateElectBlock kRootCongressNetworkId failed!");
        return kInitError;
    }

    if (CreateElectBlock(
            network::kRootCongressNetworkId,
            prehashes[network::kRootCongressNetworkId],
            vb_prehashes[network::kRootCongressNetworkId],
            pool_with_heights[common::kImmutablePoolSize]++,
            pool_with_heights[common::kImmutablePoolSize] - 1 ,
            vb_latest_view[network::kRootCongressNetworkId]++,
            root_gens_init_block_file,
            root_genesis_nodes,
            root_genesis_nodes) != kInitSuccess) {
        ZJC_FATAL("CreateElectBlock kRootCongressNetworkId failed!");
        return kInitError;
    }

    // 这也应该是 pool_index，其实就是选了 root network 的 pool 2 和 pool 3 ?
    pool_prev_hash_map[network::kRootCongressNetworkId] = prehashes[network::kRootCongressNetworkId];
    pool_prev_vb_hash_map[network::kRootCongressNetworkId] = vb_prehashes[network::kRootCongressNetworkId];
    // prehashes 不是 pool 当中前一个块的 hash 吗，为什么是 prehashes[network_id] 而不是 prehashes[pool_index]
    for (uint32_t i = 0; i < cons_genesis_nodes_of_shards.size(); i++) {
        uint32_t net_id = i + network::kConsensusShardBeginNetworkId;
        GenisisNodeInfoPtrVector genesis_nodes = cons_genesis_nodes_of_shards[i];
        if (CreateElectBlock(
                net_id,
                prehashes[net_id],
                vb_prehashes[net_id],
                pool_with_heights[net_id]++,
                common::kInvalidUint64,
                vb_latest_view[net_id]++,
                root_gens_init_block_file,
                root_genesis_nodes,
                genesis_nodes) != kInitSuccess) {
            ZJC_FATAL("CreateElectBlock kConsensusShardBeginNetworkId failed!");
            return kInitError;
        }

        if (CreateElectBlock(
                net_id,
                prehashes[net_id],
                vb_prehashes[net_id],
                pool_with_heights[net_id]++,
                pool_with_heights[net_id] - 1,
                vb_latest_view[net_id]++,
                root_gens_init_block_file,
                root_genesis_nodes,
                genesis_nodes) != kInitSuccess) {
            ZJC_FATAL("CreateElectBlock kConsensusShardBeginNetworkId failed!");
            return kInitError;
        }

        pool_prev_hash_map[net_id] = prehashes[net_id];
        pool_prev_vb_hash_map[net_id] = vb_prehashes[net_id];
    }
    
    if (all_balance != 0) {
        ZJC_FATAL("balance all error[%llu][%llu]", all_balance, common::kGenesisFoundationMaxZjc);
        return kInitError;
    }

    // pool256 中创建时间块 
    int res = GenerateRootSingleBlock(
        root_genesis_nodes, 
        root_gens_init_block_file, 
        pool_with_heights, 
        vb_latest_view);
    if (res == kInitSuccess) {
        std::vector<GenisisNodeInfoPtr> all_cons_genesis_nodes;
        for (std::vector<GenisisNodeInfoPtr> nodes : cons_genesis_nodes_of_shards) {
            all_cons_genesis_nodes.insert(all_cons_genesis_nodes.end(), nodes.begin(), nodes.end());
        }

        // 在 root 网络中为所有节点创建块
        CreateShardNodesBlocks(
            pool_prev_hash_map,
            pool_prev_vb_hash_map,
            root_genesis_nodes,
            all_cons_genesis_nodes,
            network::kRootCongressNetworkId,
            pool_with_heights,
            vb_latest_view,
            genesis_acount_balance_map);
    }

    // 统计信息初始化
    {
        uint32_t net_id = network::kRootCongressNetworkId;
        uint32_t pool_index = common::kImmutablePoolSize;
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_height(pool_with_heights[pool_index]++);
        tenon_block->set_timeblock_height(0);
        // TODO network 就是 net_id
        view_block_ptr->set_parent_hash(pool_prev_vb_hash_map[pool_index]);
        if (CreateAllQc(
                net_id,
                pool_index,
                vb_latest_view[pool_index]++, 
                root_genesis_nodes, 
                view_block_ptr) != kInitSuccess) {
            assert(false);
            return kInitError;
        }

        pool_prev_hash_map[pool_index] = view_block_ptr->qc().view_block_hash();
        pool_prev_vb_hash_map[pool_index] = view_block_ptr->qc().view_block_hash();
        pools::protobuf::PoolStatisticTxInfo& pool_st_info = *tenon_block->mutable_pool_st_info();
        pool_st_info.set_height(1);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            auto statistic_info = pool_st_info.add_pool_statisitcs();
            statistic_info->set_pool_index(i);
            statistic_info->set_min_height(pool_with_heights[i]);
            statistic_info->set_max_height(pool_with_heights[i]);
        }

        pools::protobuf::ShardToTxItem& heights = *tenon_block->mutable_to_heights();
        heights.set_sharding_id(net_id);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            heights.add_heights(pool_with_heights[i]);
        }

        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
        AddBlockItemToCache(view_block_ptr, db_batch);
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        db_->Put(db_batch);
    }

    fclose(root_gens_init_block_file);
    return res;
}

bool GenesisBlockInit::BlsAggSignViewBlock(
        const std::vector<GenisisNodeInfoPtr>& genesis_nodes,
        const view_block::protobuf::QcItem& commit_qc,
        std::shared_ptr<libff::alt_bn128_G1>& agg_sign) try {
    std::vector<libff::alt_bn128_G1> all_signs;
    uint32_t n = genesis_nodes.size();
    uint32_t t = common::GetSignerCount(n);
    std::vector<size_t> idx_vec;
    auto qc_hash = hotstuff::GetQCMsgHash(commit_qc);
    auto g1_hash = libBLS::Bls::Hashing(qc_hash);
    std::mutex mutex;
    auto sign_task = [&](uint32_t i) {
        libff::alt_bn128_G1 sign;
        bls::BlsSign::Sign(
            t,
            n,
            genesis_nodes[i]->bls_prikey,
            g1_hash,
            &sign);

        ZJC_DEBUG("use network %u, index: %d, prikey: %s",
            commit_qc.network_id(), i, 
            libBLS::ThresholdUtils::fieldElementToString(genesis_nodes[i]->bls_prikey).c_str());
        std::lock_guard<std::mutex> lock(mutex);
        all_signs.push_back(sign);
        idx_vec.push_back(i + 1);
        ZJC_DEBUG("push back i: %d, n: %d, t: %d", i, n, t);
    };

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < t; ++i) {
        sign_task(i);
        // threads.emplace_back(sign_task, i);
        // if (threads.size() >= 1 || i == t - 1) {
        //     for (uint32_t i = 0; i < threads.size(); ++i) {
        //         threads[i].join();
        //     }

        //     threads.clear();
        // }        
    }

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
    libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
    agg_sign = std::make_shared<libff::alt_bn128_G1>(
        bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
#ifndef MOCK_SIGN
    if (!libBLS::Bls::Verification(g1_hash, *agg_sign, common_pk_[commit_qc.network_id()])) {
        ZJC_FATAL("agg sign failed shard: %u, hash: %s, pk: %s",
            commit_qc.network_id(), common::Encode::HexEncode(qc_hash).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(common_pk_[commit_qc.network_id()].X.c0).c_str());
        return false;
    }
#endif

    agg_sign->to_affine_coordinates();
    ZJC_INFO("agg sign success shard: %u_%u, hash: %s, pk: %s, sign x: %s",
        commit_qc.network_id(), commit_qc.pool_index(),  common::Encode::HexEncode(qc_hash).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(common_pk_[commit_qc.network_id()].X.c0).c_str(),
        libBLS::ThresholdUtils::fieldElementToString(agg_sign->X).c_str());
    return true;
} catch (std::exception& e) {
    ZJC_ERROR("catch bls exception: %s", e.what());
    return false;
}

void GenesisBlockInit::AddBlockItemToCache(
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        db::DbWriteBatch& db_batch) {
    auto* block = &view_block->block_info();
    pools::protobuf::PoolLatestInfo pool_info;
    pool_info.set_height(block->height());
    pool_info.set_hash(view_block->qc().view_block_hash());
    pool_info.set_timestamp(block->timestamp());
    pool_info.set_view(view_block->qc().view());
    prefix_db_->SaveLatestPoolInfo(
        view_block->qc().network_id(), view_block->qc().pool_index(), pool_info, db_batch);
    ZJC_DEBUG("success add pool latest info: %u_%u_%lu, block height: %lu, tm: %lu",
        view_block->qc().network_id(), view_block->qc().pool_index(), 
        view_block->qc().view(), block->height(), block->timestamp());
    if (block->has_pool_st_info()) {
        prefix_db_->SaveLatestPoolStatisticTag(
            view_block->qc().network_id(), 
            block->pool_st_info(), 
            db_batch);
    }

    if (block->has_to_heights()) {
        prefix_db_->SaveLatestToTxsHeights(block->to_heights(), db_batch);
    }

    if (block->has_elect_block()) {
        prefix_db_->SaveLatestElectBlock(block->elect_block(), db_batch);
    }

    if (block->has_timer_block()) {
        prefix_db_->SaveLatestTimeBlock(block->timer_block(), db_batch);
    }

    prefix_db_->SaveBlock(*view_block, db_batch);
    for (uint32_t i = 0; i < view_block->block_info().address_array_size(); ++i) {
        auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>(
            view_block->block_info().address_array(i));
        prefix_db_->AddAddressInfo(new_addr_info->addr(), *new_addr_info, zjc_host_ptr->db_batch_);
        ZJC_DEBUG("step: %d, success add addr: %s, value: %s", 
            0,
            common::Encode::HexEncode(new_addr_info->addr()).c_str(), 
            ProtobufToJson(*new_addr_info).c_str());
    }

    for (uint32_t i = 0; i < view_block->block_info().key_value_array_size(); ++i) {
        auto key = view_block->block_info().key_value_array(i).addr() + 
            view_block->block_info().key_value_array(i).key();
        prefix_db_->SaveTemporaryKv(
            key, 
            view_block->block_info().key_value_array(i).value(), 
            zjc_host_ptr->db_batch_);
    }

    for (uint32_t i = 0; i < block->joins_size(); ++i) {
        auto& join_info = block->joins(i);
        // 存放了一个 from => balance 的映射
        prefix_db_->SaveElectNodeStoke(
            join_info.addr(),
            view_block->qc().elect_height(),
            join_info.stoke(),
            db_batch);
        
        if (join_info.g2_req().verify_vec_size() <= 0) {
            ZJC_DEBUG("success handle kElectJoin tx: %s, not has verfications.",
                common::Encode::HexEncode(join_info.addr()).c_str());
            continue;
        }

        prefix_db_->SaveNodeVerificationVector(join_info.addr(), join_info, db_batch);
        ZJC_DEBUG("success handle kElectJoin tx: %s, net: %u, pool: %u, height: %lu, local net id: %u",
            common::Encode::HexEncode(join_info.addr()).c_str(), 
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            block->height(),
            common::GlobalInfo::Instance()->network_id());
    }

    prefix_db_->SaveValidViewBlockParentHash(
        view_block->parent_hash(), 
        view_block->qc().network_id(),
        view_block->qc().pool_index(),
        view_block->qc().view(),
        db_batch);
}

// 在 net_id 中为 shard 节点创建块
int GenesisBlockInit::CreateShardNodesBlocks(
        std::unordered_map<uint32_t, std::string>& pool_prev_hash_map,
        std::unordered_map<uint32_t, hotstuff::HashStr> pool_prev_vb_hash_map,
        const std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        const std::vector<GenisisNodeInfoPtr>& cons_genesis_nodes,
        uint32_t net_id,
        uint64_t* pool_with_heights,
        hotstuff::View* pool_latest_view,
        std::unordered_map<std::string, uint64_t> genesis_acount_balance_map) {
    std::map<std::string, GenisisNodeInfoPtr> valid_ids;
    for (auto iter = root_genesis_nodes.begin(); iter != root_genesis_nodes.end(); ++iter) {
        if (valid_ids.find((*iter)->id) != valid_ids.end()) {
            ZJC_FATAL("invalid id: %s", common::Encode::HexEncode((*iter)->id).c_str());
            return kInitError;
        }

        valid_ids[(*iter)->id] = *iter;
    }

    for (auto iter = cons_genesis_nodes.begin(); iter != cons_genesis_nodes.end(); ++iter) {
        if (valid_ids.find((*iter)->id) != valid_ids.end()) {
            ZJC_FATAL("invalid id: %s", common::Encode::HexEncode((*iter)->id).c_str());
            return kInitError;
        }

        valid_ids[(*iter)->id] = *iter;
    }

    // valid_ids 为所有节点（包括 root 和 shard）address
    uint64_t all_balance = 0llu;
    uint64_t expect_all_balance = 0;
    int32_t idx = 0;
    // 每个节点都要创建一个块
    for (auto iter = valid_ids.begin(); iter != valid_ids.end(); ++iter, ++idx) {
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        auto tx_list = tenon_block->mutable_tx_list();
        uint64_t genesis_account_balance = 0;
        auto balance_iter = genesis_acount_balance_map.find(iter->first);
        if (balance_iter != genesis_acount_balance_map.end()) {
            genesis_account_balance = balance_iter->second;
            expect_all_balance += genesis_account_balance;
        }

        // 添加创建节点账户交易，节点账户用于选举
        {
            auto tx_info = tx_list->Add();
            tx_info->set_nonce(iter->second->nonce++);
            tx_info->set_from("");
            tx_info->set_to(iter->first);
            tx_info->set_amount(0);
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        // 一个节点地址对应一个 pool index，将 shard 节点的选举交易分配到不同的 pool 中
        auto pool_index = common::GetAddressPoolIndex(iter->first);
        for (uint32_t member_idx = 0; member_idx < cons_genesis_nodes.size(); ++member_idx) {
            if (common::GetAddressPoolIndex(cons_genesis_nodes[member_idx]->id) == pool_index) {
                auto join_elect_tx_info = tx_list->Add();
                join_elect_tx_info->set_step(pools::protobuf::kJoinElect);
                join_elect_tx_info->set_from(cons_genesis_nodes[member_idx]->id);
                join_elect_tx_info->set_to("");
                join_elect_tx_info->set_gas_limit(0);
                join_elect_tx_info->set_nonce(cons_genesis_nodes[member_idx]->nonce++);
                join_elect_tx_info->set_gas_used(0);
                join_elect_tx_info->set_status(0);
                bls::protobuf::JoinElectInfo* join_info = tenon_block->add_joins();
                *join_info = cons_genesis_nodes[member_idx]->g2_val;
                join_elect_tx_info->set_amount(0);
                join_elect_tx_info->set_balance(genesis_account_balance);
            }
        }

        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_height(pool_with_heights[pool_index]++);
        tenon_block->set_timeblock_height(0);
        // TODO network 就是 net_id
        view_block_ptr->set_parent_hash(pool_prev_vb_hash_map[pool_index]);
        if (net_id == network::kRootCongressNetworkId) {
            if (CreateAllQc(
                    net_id,
                    pool_index,
                    pool_latest_view[pool_index]++, 
                    root_genesis_nodes, 
                    view_block_ptr) != kInitSuccess) {
                assert(false);
                return kInitError;
            }
        } else {
            if (CreateAllQc(
                    net_id,
                    pool_index,
                    pool_latest_view[pool_index]++, 
                    cons_genesis_nodes, 
                    view_block_ptr) != kInitSuccess) {
                assert(false);
                return kInitError;
            }
        }

        pool_prev_hash_map[pool_index] = view_block_ptr->qc().view_block_hash();
        pool_prev_vb_hash_map[pool_index] = view_block_ptr->qc().view_block_hash();
        //         INIT_DEBUG("add genesis block account id: %s", common::Encode::HexEncode(address).c_str());
        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
        AddBlockItemToCache(view_block_ptr, db_batch);
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        
        // for (uint32_t i = 0; i < cons_genesis_nodes.size(); ++i) {
        // for (int32_t tx_idx = 0; tx_idx < tenon_block->tx_list_size(); ++tx_idx) {
        //     if (tenon_block->tx_list(tx_idx).step() == pools::protobuf::kJoinElect) {
        //         block_mgr_->HandleJoinElectTx(*view_block_ptr, tenon_block->tx_list(tx_idx), db_batch);
        //     }
        // }
        // }
        // root 网络节点账户状态都在 shard3 中
        if (net_id == network::kRootCongressNetworkId) {
            block_mgr_->GenesisAddAllAccount(network::kConsensusShardBeginNetworkId, tenon_block_ptr, db_batch);
        } else {
            block_mgr_->GenesisAddAllAccount(net_id, tenon_block_ptr, db_batch);
        }
        
        db_->Put(db_batch);
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(iter->first);
        if (account_ptr == nullptr) {
            ZJC_FATAL("get address failed! [%s]", common::Encode::HexEncode(iter->first).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != genesis_account_balance) {
            ZJC_FATAL("get address balance failed! [%s]", common::Encode::HexEncode(iter->first).c_str());
            return kInitError;
        }
        all_balance += account_ptr->balance();
        ZJC_INFO("new address %s, genesis balance: %lu, nonce: %lu",
            common::Encode::HexEncode(account_ptr->addr()).c_str(), 
            account_ptr->balance(),
            account_ptr->nonce());
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
    // 每个账户分配余额，只有 shard3 中的合法账户会被分配
    uint64_t genesis_account_balance = common::kGenesisFoundationMaxZjc / net_pool_index_map_addr_count_; // 两个分片
    std::unordered_map<uint32_t, std::string> pool_prev_hash_map;
    std::unordered_map<uint32_t, hotstuff::HashStr> pool_prev_vb_hash_map;
    // view 从 0 开始
    hotstuff::View vb_latest_view[common::kInvalidPoolIndex] = {0};
    uint64_t pool_with_heights[common::kInvalidPoolIndex] = {0};
    
    // 给每个账户在 net_id 网络中创建块，并分配到不同的 pool 当中
    for (uint32_t i = 0; i < common::kImmutablePoolSize + 1; ++i) {
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        auto tx_list = tenon_block->mutable_tx_list();
        if (i >= common::kImmutablePoolSize) {
            auto address_info = immutable_pool_address_info_;
            auto tx_info = tx_list->Add();
            tx_info->set_nonce(address_info->nonce());
            address_info->set_nonce(address_info->nonce() + 1);
            tx_info->set_from("");
            tx_info->set_to(address_info->addr());
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        } else {
            auto tx_info = tx_list->Add();
            tx_info->set_to(pool_address_info_[i]->addr());
            tx_info->set_nonce(pool_address_info_[i]->nonce());
            pool_address_info_[i]->set_nonce(pool_address_info_[i]->nonce() + 1);
            tx_info->set_from("");
            tx_info->set_amount(0); // 余额 0 即可
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
            // root 创世账户也创建在 shard3?
        }
        
        auto& pool_map = net_pool_index_map_[net_id];
        auto pool_iter = pool_map.find(i);
        if (pool_iter != pool_map.end()) {
            for (auto addr_iter = pool_iter->second.begin(); addr_iter != pool_iter->second.end(); ++addr_iter) {
                // 向 shard 账户转账，root 网络中的账户余额不重要，主要是记录下此 block 的 shard 信息即可
                auto tx_info = tx_list->Add();
                tx_info->set_from("");
                tx_info->set_to(addr_iter->first);
                tx_info->set_nonce(addr_iter->second++);
                tx_info->set_amount(genesis_account_balance);
                tx_info->set_balance(genesis_account_balance);
                tx_info->set_gas_limit(0);
                tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
                ZJC_DEBUG("net_id: %d, success add address: %s, balance: %lu",
                    net_id, common::Encode::HexEncode(addr_iter->first).c_str(), genesis_account_balance);
            }
        }
        
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_height(pool_with_heights[i]++);
        tenon_block->set_timeblock_height(0);
        view_block_ptr->set_parent_hash("");
        if (CreateAllQc(
                net_id,
                i,
                vb_latest_view[i]++, 
                cons_genesis_nodes, 
                view_block_ptr) != kInitSuccess) {
            assert(false);
            return kInitError;
        }

        // 更新所有 pool 的 prehash
        pool_prev_hash_map[i] = view_block_ptr->qc().view_block_hash();
        pool_prev_vb_hash_map[i] = view_block_ptr->qc().view_block_hash();

        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;
        // 更新 pool 最新信息
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
        AddBlockItemToCache(view_block_ptr, db_batch);
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        block_mgr_->GenesisAddAllAccount(net_id, tenon_block_ptr, db_batch);
        db_->Put(db_batch);
    }

    CreateShardNodesBlocks(
            pool_prev_hash_map,
            pool_prev_vb_hash_map,
            root_genesis_nodes,
            cons_genesis_nodes,
            net_id,
            pool_with_heights,
            vb_latest_view,
            genesis_acount_balance_map);
    // 统计信息初始化
    {
        uint32_t pool_index = common::kImmutablePoolSize;
        auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* tenon_block = view_block_ptr->mutable_block_info();
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_height(pool_with_heights[pool_index]++);
        tenon_block->set_timeblock_height(0);
        // TODO network 就是 net_id
        view_block_ptr->set_parent_hash(pool_prev_vb_hash_map[pool_index]);
        if (CreateAllQc(
                net_id,
                pool_index,
                vb_latest_view[pool_index]++, 
                cons_genesis_nodes, 
                view_block_ptr) != kInitSuccess) {
            assert(false);
            return kInitError;
        }

        pool_prev_hash_map[pool_index] = view_block_ptr->qc().view_block_hash();
        pool_prev_vb_hash_map[pool_index] = view_block_ptr->qc().view_block_hash();
        pools::protobuf::PoolStatisticTxInfo& pool_st_info = *tenon_block->mutable_pool_st_info();
        pool_st_info.set_height(1);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            auto statistic_info = pool_st_info.add_pool_statisitcs();
            statistic_info->set_pool_index(i);
            statistic_info->set_min_height(pool_with_heights[i]);
            statistic_info->set_max_height(pool_with_heights[i]);
        }

        pools::protobuf::ShardToTxItem& heights = *tenon_block->mutable_to_heights();
        heights.set_sharding_id(net_id);
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            heights.add_heights(pool_with_heights[i]);
        }

        auto db_batch_ptr = std::make_shared<db::DbWriteBatch>();
        auto& db_batch = *db_batch_ptr;
        auto tenon_block_ptr = std::make_shared<block::protobuf::Block>(*tenon_block);
        AddBlockItemToCache(view_block_ptr, db_batch);
        block_mgr_->GenesisNewBlock(view_block_ptr, db_batch);
        db_->Put(db_batch);
    }
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

    auto load_addrs_func = [&](uint32_t net_id, const char* filename) {
        auto fd = fopen(filename, "r");
        assert(fd != nullptr);
        char data[1024 * 1024];
        fread(data, 1, sizeof(data), fd);
        auto lines = common::Split<>(data, '\n');
        auto& pool_index_map = net_pool_index_map_[net_id];
        for (int32_t i = 0; i < lines.Count(); ++i) {
            auto items = common::Split<>(lines[i], '\t');
            if (items.Count() != 2) {
                break;
            }

            std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
            secptr->SetPrivateKey(common::Encode::HexDecode(items[0]));
            auto pool_idx = common::GetAddressPoolIndex(secptr->GetAddress());
            pool_index_map[pool_idx][secptr->GetAddress()] = 0;
            ++net_pool_index_map_addr_count_;
            ZJC_DEBUG("success add address net: %d, pool: %d, addr: %s", 
                net_id, pool_idx, common::Encode::HexEncode(secptr->GetAddress()).c_str());
        }

        fclose(fd);
    };

    if (!hasRunOnce) {
        load_addrs_func(network::kConsensusShardBeginNetworkId, "/root/shardora/root_nodes");
        for (uint32_t net_id = network::kConsensusShardBeginNetworkId;
                net_id < network::kConsensusShardEndNetworkId; net_id++) {
            load_addrs_func(net_id, (std::string("/root/shardora/init_accounts") + std::to_string(net_id)).c_str());
            load_addrs_func(net_id, (std::string("/root/shardora/shards") + std::to_string(net_id)).c_str());
        }    
    }

    hasRunOnce = true;
}

void GenesisBlockInit::PrintGenisisAccounts() {
    db::DbReadOptions option;
    auto iter = db_->db()->NewIterator(option);
    iter->Seek(protos::kAddressPrefix);
    int32_t valid_count = 0;
    while (iter->Valid()) {
        if (memcmp(
                protos::kAddressPrefix.c_str(), 
                iter->key().data(), 
                protos::kAddressPrefix.size()) != 0) {
            break;
        }

        address::protobuf::AddressInfo addr_info;
        if (!addr_info.ParseFromString(iter->value().ToString())) {
            assert(false);
        }

        std::cout << common::Encode::HexEncode(addr_info.addr()) << ", " 
            << addr_info.balance() << ", " << addr_info.nonce() << std::endl;
        iter->Next();
    }

    delete iter;
}

};  // namespace init

};  // namespace shardora
