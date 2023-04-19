#include "init/genesis_block_init.h"

#include <cmath>
#include <vector>

#include "common/encode.h"
#include "common/random.h"
#include "common/split.h"
#include "block/account_manager.h"
#include "block/block_manager.h"
#include "consensus/consensus_utils.h"
#include "consensus/zbft/zbft_utils.h"
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "init/init_utils.h"
#include "pools/tx_pool_manager.h"
#include "protos/zbft.pb.h"
#include "security/ecdsa/ecdsa.h"
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

int GenesisBlockInit::CreateGenesisBlocks(
        uint32_t net_id,
        const std::vector<dht::NodePtr>& root_genesis_nodes,
        const std::vector<dht::NodePtr>& cons_genesis_nodes) {
    for (uint32_t i = 0; i < root_genesis_nodes.size(); ++i) {
        root_bitmap_.Set(i);
    }

    for (uint32_t i = 0; i < cons_genesis_nodes.size(); ++i) {
        shard_bitmap_.Set(i);
    }

    int res = kInitSuccess;
    std::shared_ptr<pools::TxPoolManager> pools_mgr = nullptr;
    std::shared_ptr<pools::ShardStatistic> statistic_mgr = nullptr;
    account_mgr_->Init(1, db_, pools_mgr);
    block_mgr_->Init(account_mgr_, db_, pools_mgr, statistic_mgr, "", nullptr);
    if (net_id == network::kRootCongressNetworkId) {
        common::GlobalInfo::Instance()->set_network_id(network::kRootCongressNetworkId);
        res = CreateRootGenesisBlocks(root_genesis_nodes, cons_genesis_nodes);
    } else {
        common::GlobalInfo::Instance()->set_network_id(net_id);
        res = CreateShardGenesisBlocks(net_id);
    }

    InitBlsVerificationValue();
    db_->ClearPrefix("db_for_gid_");
    assert(res == kInitSuccess);
    return res;
}

void GenesisBlockInit::InitBlsVerificationValue() {
    FILE* rlocal_bls_fd = fopen("./saved_verify_one", "r");
    if (rlocal_bls_fd != nullptr) {
        char* line = new char[1024 * 1024];
        uint32_t idx = 0;
        while (!feof(rlocal_bls_fd)) {
            fgets(line, 1024 * 1024, rlocal_bls_fd);
            std::string val = common::Encode::HexDecode(std::string(line, strlen(line) - 1));
            uint32_t* int_data = (uint32_t*)val.c_str();
            uint32_t idx = int_data[1];
            bls::protobuf::BlsVerifyValue verify_val;
            if (!verify_val.ParseFromArray(val.c_str() + 8, val.size() - 8)) {
                ZJC_FATAL("parse BlsVerifyValue failed!");
            }

            prefix_db_->SavePresetVerifyValue(idx, 0, verify_val);
            ++idx;
            if (idx >= 1024) {
                break;
            }
        }

        delete[] line;
        fclose(rlocal_bls_fd);
    }
}

int GenesisBlockInit::CreateBlsGenesisKeys(
        uint64_t elect_height,
        uint32_t sharding_id,
        const std::vector<std::string>& prikeys,
        elect::protobuf::PrevMembers* prev_members) {
    static const uint32_t valid_n = 3;
    static const uint32_t valid_t = common::GetSignerCount(valid_n);
    libBLS::Dkg dkg_instance = libBLS::Dkg(valid_t, valid_n);
    std::vector<std::vector<libff::alt_bn128_Fr>> polynomial(valid_n);
    for (auto& pol : polynomial) {
        pol = std::vector<libff::alt_bn128_Fr>(valid_n, libff::alt_bn128_Fr::one());
        pol[0] = libff::alt_bn128_Fr::random_element();
    }

    std::vector<std::vector<libff::alt_bn128_Fr>> secret_key_contribution(valid_n);
    for (size_t i = 0; i < valid_n; ++i) {
        secret_key_contribution[i] = dkg_instance.SecretKeyContribution(
            polynomial[i], valid_n, valid_t);
    }

    std::vector<std::vector<libff::alt_bn128_G2>> verification_vector(valid_n);
    for (size_t i = 0; i < valid_n; ++i) {
        verification_vector[i] = dkg_instance.VerificationVector(polynomial[i]);
    }

    for (size_t i = 0; i < valid_n; ++i) {
        for (size_t j = i; j < valid_n; ++j) {
            std::swap(secret_key_contribution[j][i], secret_key_contribution[i][j]);
        }
    }

    FILE* fd = fopen(
        std::string(std::string("./bls_pri_") + std::to_string(sharding_id)).c_str(),
        "w");
    auto common_public_key = libff::alt_bn128_G2::zero();
    for (uint32_t idx = 0; idx < prikeys.size(); ++idx) {
        std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
        secptr->SetPrivateKey(prikeys[idx]);
        libBLS::Dkg tmpdkg(valid_t, valid_n);
        auto local_sec_key = tmpdkg.SecretKeyShareCreate(secret_key_contribution[idx]);
        auto local_publick_key = tmpdkg.GetPublicKeyFromSecretKey(local_sec_key);
        auto local_pk_ptr = std::make_shared<BLSPublicKey>(local_publick_key);
        auto mem_pk = prev_members->add_bls_pubkey();
        auto pkeys_str = local_pk_ptr->toString();
        mem_pk->set_x_c0(pkeys_str->at(0));
        mem_pk->set_x_c1(pkeys_str->at(1));
        mem_pk->set_y_c0(pkeys_str->at(2));
        mem_pk->set_y_c1(pkeys_str->at(3));
        if (idx == 0) {
            mem_pk->set_pool_idx_mod_num(0);
        } else {
            mem_pk->set_pool_idx_mod_num(-1);
        }

        auto& g2_vec = verification_vector[idx];
        common_public_key = common_public_key + g2_vec[0];
        DumpLocalPrivateKey(
            sharding_id,
            elect_height,
            secptr->GetAddress(),
            prikeys[idx],
            libBLS::ThresholdUtils::fieldElementToString(local_sec_key),
            fd);
    }

    fclose(fd);
    auto common_pk_ptr = std::make_shared<BLSPublicKey>(common_public_key);
    auto common_pk_strs = common_pk_ptr->toString();
    auto common_pk = prev_members->mutable_common_pubkey();
    common_pk->set_x_c0(common_pk_strs->at(0));
    common_pk->set_x_c1(common_pk_strs->at(1));
    common_pk->set_y_c0(common_pk_strs->at(2));
    common_pk->set_y_c1(common_pk_strs->at(3));
    return kInitSuccess;
}

void GenesisBlockInit::DumpLocalPrivateKey(
        uint32_t shard_netid,
        uint64_t height,
        const std::string& id,
        const std::string& prikey,
        const std::string& sec_key,
        FILE* fd) {
    // encrypt by private key and save to db
    std::string enc_data;
    security::Ecdsa ecdsa;
    if (ecdsa.Encrypt(
            sec_key,
            prikey,
            &enc_data) != security::kSecuritySuccess) {
        return;
    }

    prefix_db_->SaveBlsPrikey(height, shard_netid, id, enc_data);
    char data[16];
    uint64_t* tmp = (uint64_t*)data;
    tmp[0] = height;
    tmp[1] = shard_netid;
    std::string val = common::Encode::HexEncode(std::string(data, sizeof(data)) + id + enc_data) + "\n";
    fputs(val.c_str(), fd);
}

void GenesisBlockInit::ReloadBlsPri(uint32_t sharding_id) {
    FILE* bls_fd = fopen(
        std::string(std::string("./bls_pri_") + std::to_string(sharding_id)).c_str(),
        "r");
    assert(bls_fd != nullptr);
    char data[20480];
    while (fgets(data, 20480, bls_fd) != nullptr) {
        std::string tmp_data(data, strlen(data) - 1);
        std::string val = common::Encode::HexDecode(tmp_data);
        uint64_t* tmp = (uint64_t*)val.c_str();
        uint64_t height = tmp[0];
        std::string id = val.substr(16, 20);
        std::string bls_prikey = val.substr(36, val.size() - 36);
        prefix_db_->SaveBlsPrikey(height, sharding_id, id, bls_prikey);
        ZJC_DEBUG("success add bls prikey: %lu, %u, %s",
            height, sharding_id, common::Encode::HexEncode(id).c_str());
    }

    fclose(bls_fd);
}

int GenesisBlockInit::CreateElectBlock(
        uint32_t shard_netid,
        std::string& root_pre_hash,
        uint64_t height,
        uint64_t prev_height,
        FILE* root_gens_init_block_file,
        const std::vector<dht::NodePtr>& genesis_nodes) {
    auto tenon_block = std::make_shared<block::protobuf::Block>();
    auto tx_list = tenon_block->mutable_tx_list();
    auto tx_info = tx_list->Add();
    tx_info->set_step(pools::protobuf::kConsensusRootElectShard);
    tx_info->set_from("");
    tx_info->set_to(common::kRootChainElectionBlockTxAddress);
    tx_info->set_amount(0);
    tx_info->set_gas_limit(0);
    tx_info->set_gas_used(0);
    tx_info->set_balance(0);
    tx_info->set_status(0);
    elect::protobuf::ElectBlock ec_block;
    int32_t expect_leader_count = (int32_t)pow(2.0, (double)((int32_t)log2(double(genesis_nodes.size() / 3))));
    int32_t node_idx = 0;
    for (auto iter = genesis_nodes.begin(); iter != genesis_nodes.end(); ++iter) {
        auto in = ec_block.add_in();
        in->set_public_ip(common::IpToUint32("127.0.0.1"));
        in->set_pubkey((*iter)->pubkey_str);
        in->set_pool_idx_mod_num(node_idx < expect_leader_count ? node_idx : -1);
        ++node_idx;
    }

    ec_block.set_shard_network_id(shard_netid);
    std::vector<std::string> prikeys;
    if (shard_netid == 2) {
        prikeys.push_back(common::Encode::HexDecode(
            "67dfdd4d49509691369225e9059934675dea440d123aa8514441aa6788354016"));
        prikeys.push_back(common::Encode::HexDecode(
            "356bcb89a431c911f4a57109460ca071701ec58983ec91781a6bd73bde990efe"));
        prikeys.push_back(common::Encode::HexDecode(
            "a094b020c107852505385271bf22b4ab4b5211e0c50b7242730ff9a9977a77ee"));
    } else if (shard_netid == 3) {
        prikeys.push_back(common::Encode::HexDecode(
            "e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269"));
        prikeys.push_back(common::Encode::HexDecode(
            "b16e3d5523d61f0b0ccdf1586aeada079d02ccf15da9e7f2667cb6c4168bb5f0"));
        prikeys.push_back(common::Encode::HexDecode(
            "0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db995"));
    }

    if (!prikeys.empty() && prev_height != common::kInvalidUint64) {
        auto prev_members = ec_block.mutable_prev_members();
        CreateBlsGenesisKeys(prev_height, shard_netid, prikeys, prev_members);
        prev_members->set_prev_elect_height(prev_height);
    }

    tenon_block->set_height(height);
    ec_block.set_elect_height(tenon_block->height());
    auto storage = tx_info->add_storages();
    storage->set_key(protos::kElectNodeAttrElectBlock);
    storage->set_val_hash(ec_block.SerializeAsString());
    tenon_block->set_prehash(root_pre_hash);
    tenon_block->set_version(common::kTransactionVersion);
    tenon_block->set_pool_index(common::kRootChainPoolIndex);
    const auto& bitmap_data = root_bitmap_.data();
    for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
        tenon_block->add_precommit_bitmap(bitmap_data[i]);
    }

    tenon_block->set_network_id(network::kRootCongressNetworkId);
    tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
    prefix_db_->SaveLatestElectBlock(ec_block);
    fputs((common::Encode::HexEncode(tenon_block->SerializeAsString()) + "\n").c_str(),
        root_gens_init_block_file);
    db::DbWriteBatch db_batch;
    AddBlockItemToCache(tenon_block, db_batch);
    db_->Put(db_batch);
    block_mgr_->NetworkNewBlock(0, tenon_block);
//     std::string pool_hash;
//     uint64_t pool_height = 0;
//     uint64_t tm_height;
//     uint64_t tm_with_block_height;
//     int res = account_mgr_->GetBlockInfo(
//         common::kRootChainPoolIndex,
//         &pool_height,
//         &pool_hash,
//         &tm_height,
//         &tm_with_block_height);
//     if (res != block::kBlockSuccess) {
//         INIT_ERROR("GetBlockInfo error.");
//         return kInitError;
//     }

    auto account_ptr = account_mgr_->GetAcountInfoFromDb(
        common::kRootChainElectionBlockTxAddress);
    if (account_ptr == nullptr) {
        INIT_ERROR("get address failed! [%s]",
            common::Encode::HexEncode(common::kRootChainElectionBlockTxAddress).c_str());
        return kInitError;
    }

    if (account_ptr->balance() != 0) {
        INIT_ERROR("get address balance failed! [%s]",
            common::Encode::HexEncode(common::kRootChainElectionBlockTxAddress).c_str());
        return kInitError;
    }

    uint64_t elect_height = 0;
    std::string elect_block_str;
//     if (account_ptr->GetLatestElectBlock(
//             shard_netid,
//             &elect_height,
//             &elect_block_str) != block::kBlockSuccess) {
//         INIT_ERROR("get address elect block failed! [%s]",
//             common::Encode::HexEncode(common::kRootChainElectionBlockTxAddress).c_str());
//         return kInitError;
//     }

    root_pre_hash = consensus::GetBlockHash(*tenon_block);
    return kInitSuccess;
}

int GenesisBlockInit::GenerateRootSingleBlock(
        const std::vector<dht::NodePtr>& root_genesis_nodes,
        const std::vector<dht::NodePtr>& cons_genesis_nodes,
        uint64_t* root_pool_height) {
    FILE* root_gens_init_block_file = fopen("./root_blocks", "w");
    if (root_gens_init_block_file == nullptr) {
        return kInitError;
    }

    GenerateRootAccounts();
    uint64_t root_single_block_height = 0llu;
    // for root single block chain
    std::string root_pre_hash;
    {
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_gid(common::CreateGID(""));
        tx_info->set_from("");
        tx_info->set_to(common::kRootChainSingleBlockTxAddress);
        tx_info->set_amount(0);
        tx_info->set_balance(0);
        tx_info->set_gas_limit(0);
        tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        tenon_block->set_prehash("");
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(common::kRootChainPoolIndex);
        tenon_block->set_height(root_single_block_height++);
        const auto& bitmap_data = root_bitmap_.data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            tenon_block->add_precommit_bitmap(bitmap_data[i]);
        }

        tenon_block->set_network_id(common::GlobalInfo::Instance()->network_id());
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        fputs((common::Encode::HexEncode(tenon_block->SerializeAsString()) + "\n").c_str(),
            root_gens_init_block_file);
        db::DbWriteBatch db_batch;
        AddBlockItemToCache(tenon_block, db_batch);
        db_->Put(db_batch);
        block_mgr_->NetworkNewBlock(0, tenon_block);
        std::string pool_hash;
        uint64_t pool_height = 0;
        uint64_t tm_height;
        uint64_t tm_with_block_height;
//         int res = account_mgr_->GetBlockInfo(
//             common::kRootChainPoolIndex,
//             &pool_height,
//             &pool_hash,
//             &tm_height,
//             &tm_with_block_height);
//         if (res != block::kBlockSuccess) {
//             INIT_ERROR("GetBlockInfo error.");
//             return kInitError;
//         }

        auto account_ptr = account_mgr_->GetAcountInfoFromDb(
            common::kRootChainSingleBlockTxAddress);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address failed! [%s]",
                common::Encode::HexEncode(common::kRootChainSingleBlockTxAddress).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != 0) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(common::kRootChainSingleBlockTxAddress).c_str());
            return kInitError;
        }

        root_pre_hash = consensus::GetBlockHash(*tenon_block);
    }

    {
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_gid(common::CreateGID(""));
        tx_info->set_from("");
        tx_info->set_to(common::kRootChainTimeBlockTxAddress);
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
//         auto vss_random_attr = tx_info->add_attr();
//         vss_random_attr->set_key(tmblock::kVssRandomAttr);
//         vss_random_attr->set_value(std::to_string(now_tm));
        tenon_block->set_prehash(root_pre_hash);
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(common::kRootChainPoolIndex);
        const auto& bitmap_data = root_bitmap_.data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            tenon_block->add_precommit_bitmap(bitmap_data[i]);
        }

        tenon_block->set_network_id(common::GlobalInfo::Instance()->network_id());
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        auto tmp_str = tenon_block->SerializeAsString();
        block::protobuf::Block tenon_block2;
        tenon_block2.ParseFromString(tmp_str);
        assert(tenon_block2.tx_list_size() > 0);
        db::DbWriteBatch db_batch;
        prefix_db_->SaveLatestTimeBlock(tenon_block->height(), db_batch);
        prefix_db_->SaveConsensusedStatisticTimeBlockHeight(
            network::kRootCongressNetworkId, tenon_block->height(), db_batch);
        fputs((common::Encode::HexEncode(tmp_str) + "\n").c_str(), root_gens_init_block_file);
//         tmblock::TimeBlockManager::Instance()->UpdateTimeBlock(1, now_tm, now_tm);
        AddBlockItemToCache(tenon_block, db_batch);
        db_->Put(db_batch);
        block_mgr_->NetworkNewBlock(0, tenon_block);
        std::string pool_hash;
        uint64_t pool_height = 0;
        uint64_t tm_height;
        uint64_t tm_with_block_height;
//         int res = account_mgr_->GetBlockInfo(
//             common::kRootChainPoolIndex,
//             &pool_height,
//             &pool_hash,
//             &tm_height,
//             &tm_with_block_height);
//         if (res != block::kBlockSuccess) {
//             INIT_ERROR("GetBlockInfo error");
//             return kInitError;
//         }

        auto account_ptr = account_mgr_->GetAcountInfoFromDb(
            common::kRootChainTimeBlockTxAddress);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(common::kRootChainTimeBlockTxAddress).c_str());
            assert(false);
            return kInitError;
        }

        if (account_ptr->balance() != 0) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(common::kRootChainTimeBlockTxAddress).c_str());
            assert(false);
            return kInitError;
        }

        root_pre_hash = consensus::GetBlockHash(*tenon_block);
    }

    uint64_t root_prev_elect_height = root_single_block_height;
    if (CreateElectBlock(
            network::kRootCongressNetworkId,
            root_pre_hash,
            root_single_block_height++,
            common::kInvalidUint64,
            root_gens_init_block_file,
            root_genesis_nodes) != kInitSuccess) {
        INIT_ERROR("CreateElectBlock kRootCongressNetworkId failed!");
        return kInitError;
    }

    if (CreateElectBlock(
            network::kRootCongressNetworkId,
            root_pre_hash,
            root_single_block_height++,
            root_prev_elect_height,
            root_gens_init_block_file,
            root_genesis_nodes) != kInitSuccess) {
        INIT_ERROR("CreateElectBlock kRootCongressNetworkId failed!");
        return kInitError;
    }

    uint64_t shard_prev_elect_height = root_single_block_height;
    if (CreateElectBlock(
            network::kConsensusShardBeginNetworkId,
            root_pre_hash,
            root_single_block_height++,
            common::kInvalidUint64,
            root_gens_init_block_file,
            cons_genesis_nodes) != kInitSuccess) {
        INIT_ERROR("CreateElectBlock kConsensusShardBeginNetworkId failed!");
        return kInitError;
    }

    if (CreateElectBlock(
            network::kConsensusShardBeginNetworkId,
            root_pre_hash,
            root_single_block_height++,
            shard_prev_elect_height,
            root_gens_init_block_file,
            cons_genesis_nodes) != kInitSuccess) {
        INIT_ERROR("CreateElectBlock kConsensusShardBeginNetworkId failed!");
        return kInitError;
    }

    fclose(root_gens_init_block_file);
    *root_pool_height = root_single_block_height;
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
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        std::string tmp_data(data, strlen(data) - 1);
        if (!tenon_block->ParseFromString(common::Encode::HexDecode(tmp_data))) {
            assert(false);
            return kInitError;
        }

        AddBlockItemToCache(tenon_block, db_batch);
        block_mgr_->NetworkNewBlock(0, tenon_block);
        for (int32_t i = 0; i < tenon_block->tx_list_size(); ++i) {
            for (int32_t j = 0; j < tenon_block->tx_list(i).storages_size(); ++j) {
                if (tenon_block->tx_list(i).storages(j).key() == protos::kElectNodeAttrElectBlock) {
                    elect::protobuf::ElectBlock ec_block;
                    if (!ec_block.ParseFromString(tenon_block->tx_list(i).storages(j).val_hash())) {
                        assert(false);
                    }

                    prefix_db_->SaveLatestElectBlock(ec_block);
                }

                if (tenon_block->tx_list(i).storages(j).key() == protos::kAttrTimerBlock) {
                    prefix_db_->SaveLatestTimeBlock(tenon_block->height(), db_batch);
                    prefix_db_->SaveConsensusedStatisticTimeBlockHeight(
                        sharding_id, tenon_block->height(), db_batch);
                }
            }
        }
    }

    db_->Put(db_batch);
    {
        auto address = common::kRootChainSingleBlockTxAddress;
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address info failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != 0) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }
    }
        
    {
        auto address = common::kRootChainTimeBlockTxAddress;
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address info failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != 0) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }
    }
        
    {
        auto address = common::kRootChainElectionBlockTxAddress;
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address info failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != 0) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }
    }

    ReloadBlsPri(sharding_id);
    return kInitSuccess;
}

std::string GenesisBlockInit::GetValidPoolBaseAddr(uint32_t network_id, uint32_t pool_index) {
    uint32_t id_idx = 0;
    while (true) {
        std::string addr = common::Encode::HexDecode(common::StringUtil::Format(
            "%04d%s%04d",
            network_id,
            common::kStatisticFromAddressMidllefix.c_str(),
            id_idx++));
        uint32_t pool_idx = common::GetBasePoolIndex(addr);
        if (pool_idx == pool_index) {
            return addr;
        }
    }
}

int GenesisBlockInit::CreateRootGenesisBlocks(
        const std::vector<dht::NodePtr>& root_genesis_nodes,
        const std::vector<dht::NodePtr>& cons_genesis_nodes) {
    GenerateRootAccounts();
    InitGenesisAccount();
    uint64_t genesis_account_balance = 0llu;
    uint64_t all_balance = 0llu;
    pools::protobuf::ToTxHeights init_heights;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        auto iter = root_account_with_pool_index_map_.find(i);
        auto shard_iter = pool_index_map_.find(i);
        std::string address = iter->second;
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(GetValidPoolBaseAddr(
                network::kConsensusShardBeginNetworkId,
                common::GetBasePoolIndex(address)));
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(GetValidPoolBaseAddr(
                network::kRootCongressNetworkId,
                common::GetBasePoolIndex(address)));
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(address);
            tx_info->set_amount(genesis_account_balance);
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(shard_iter->second);
            tx_info->set_amount(genesis_account_balance);
            tx_info->set_balance(genesis_account_balance);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }

        tenon_block->set_prehash("");
        tenon_block->set_version(common::kTransactionVersion);
        tenon_block->set_pool_index(iter->first);
        tenon_block->set_height(0);
        const auto& bitmap_data = root_bitmap_.data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            tenon_block->add_precommit_bitmap(bitmap_data[i]);
        }

        tenon_block->set_timeblock_height(1);
        tenon_block->set_electblock_height(2);
        tenon_block->set_network_id(common::GlobalInfo::Instance()->network_id());
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
        db::DbWriteBatch db_batch;
        AddBlockItemToCache(tenon_block, db_batch);
        db_->Put(db_batch);
        block_mgr_->NetworkNewBlock(0, tenon_block);
        init_heights.add_heights(0);
        //         std::string pool_hash;
//         uint64_t pool_height = 0;
//         uint64_t tm_height;
//         uint64_t tm_with_block_height;
//         int res = account_mgr_->GetBlockInfo(
//             iter->first,
//             &pool_height,
//             &pool_hash,
//             &tm_height,
//             &tm_with_block_height);
//         if (res != block::kBlockSuccess) {
//             INIT_ERROR("get pool block info failed! [%u]", iter->first);
//             return kInitError;
//         }
// 
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address info failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != genesis_account_balance) {
            INIT_ERROR("get address balance failed! [%s]",
                common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        all_balance += account_ptr->balance();
    }

    if (all_balance != 0) {
        INIT_ERROR("balance all error[%llu][%llu]", all_balance, common::kGenesisFoundationMaxZjc);
        return kInitError;
    }

    uint64_t root_pool_height = 0;
    int res = GenerateRootSingleBlock(root_genesis_nodes, cons_genesis_nodes, &root_pool_height);
    if (res == kInitSuccess) {
        init_heights.add_heights(root_pool_height);
        prefix_db_->SaveStatisticLatestHeihgts(network::kRootCongressNetworkId, init_heights);
    }

    return res;
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

int GenesisBlockInit::CreateShardGenesisBlocks(uint32_t net_id) {
    InitGenesisAccount();
    uint64_t genesis_account_balance = common::kGenesisFoundationMaxZjc / pool_index_map_.size();
    uint64_t all_balance = 0llu;
    pools::protobuf::ToTxHeights init_heights;
    for (auto iter = pool_index_map_.begin(); iter != pool_index_map_.end(); ++iter) {
        auto tenon_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = tenon_block->mutable_tx_list();
        std::string address = iter->second;
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(GetValidPoolBaseAddr(
                network::kConsensusShardBeginNetworkId,
                common::GetBasePoolIndex(address)));
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from(GetValidPoolBaseAddr(
                network::kRootCongressNetworkId,
                common::GetBasePoolIndex(address)));
            tx_info->set_to("");
            tx_info->set_amount(0);
            tx_info->set_balance(0);
            tx_info->set_gas_limit(0);
            tx_info->set_step(pools::protobuf::kConsensusCreateGenesisAcount);
        }
        {
            auto tx_info = tx_list->Add();
            tx_info->set_gid(common::CreateGID(""));
            tx_info->set_from("");
            tx_info->set_to(address);

            if (iter->first == common::kImmutablePoolSize - 1) {
                genesis_account_balance += common::kGenesisFoundationMaxZjc % pool_index_map_.size();
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
        const auto& bitmap_data = root_bitmap_.data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            tenon_block->add_precommit_bitmap(bitmap_data[i]);
        }

        tenon_block->set_timeblock_height(1);
        tenon_block->set_electblock_height(2);
        tenon_block->set_network_id(common::GlobalInfo::Instance()->network_id());
        tenon_block->set_hash(consensus::GetBlockHash(*tenon_block));
//         INIT_DEBUG("add genesis block account id: %s", common::Encode::HexEncode(address).c_str());
        db::DbWriteBatch db_batch;
        AddBlockItemToCache(tenon_block, db_batch);
        db_->Put(db_batch);
        block_mgr_->NetworkNewBlock(0, tenon_block);
        auto account_ptr = account_mgr_->GetAcountInfoFromDb(address);
        if (account_ptr == nullptr) {
            INIT_ERROR("get address failed! [%s]", common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        if (account_ptr->balance() != genesis_account_balance) {
            INIT_ERROR("get address balance failed! [%s]", common::Encode::HexEncode(address).c_str());
            return kInitError;
        }

        all_balance += account_ptr->balance();
        init_heights.add_heights(0);
    }

    if (all_balance != common::kGenesisFoundationMaxZjc) {
        INIT_ERROR("all_balance != common::kGenesisFoundationMaxTenon failed! [%lu][%llu]",
            all_balance, common::kGenesisFoundationMaxZjc);
        return kInitError;
    }

    prefix_db_->SaveStatisticLatestHeihgts(net_id, init_heights);
    return GenerateShardSingleBlock(net_id);
}

void GenesisBlockInit::InitGenesisAccount() {
//     std::map<std::string, std::string> pri_pub_map;
//     while (pool_index_map_.size() < common::kImmutablePoolSize) {
//         security::PrivateKey prikey;
//         security::PublicKey pubkey(prikey);
//         std::string pubkey_str;
//         pubkey.Serialize(pubkey_str, false);
//         std::string prikey_str;
//         prikey.Serialize(prikey_str);
// 
//         std::string address = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
//         std::string address1 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(prikey_str);
//         assert(address == address1);
//         auto pool_index = common::GetBasePoolIndex(address);
//         auto iter = pool_index_map_.find(pool_index);
//         if (iter != pool_index_map_.end()) {
//             continue;
//         }
// 
//         pool_index_map_.insert(std::make_pair(pool_index, address));
//         pri_pub_map[address] = prikey_str;
//     }
// 
//     for (auto iter = pool_index_map_.begin(); iter != pool_index_map_.end(); ++iter) {
//         std::cout << common::Encode::HexEncode(iter->second) << "\t" << common::Encode::HexEncode(pri_pub_map[iter->second]) << std::endl;
//     }
// 
//     for (auto iter = pool_index_map_.begin(); iter != pool_index_map_.end(); ++iter) {
//         std::cout << "pool_index_map_.insert(std::make_pair(" << iter->first << ", common::Encode::HexDecode(\"" << common::Encode::HexEncode(iter->second) << "\")));" << std::endl;
//     }
    pool_index_map_.insert(std::make_pair(0, common::Encode::HexDecode("d83be8753e637a02b2314ae7a9ece76676ab2d84")));
    pool_index_map_.insert(std::make_pair(1, common::Encode::HexDecode("ff80674ebc69f7764ba8b2595d495287bc3a133c")));
    pool_index_map_.insert(std::make_pair(2, common::Encode::HexDecode("6fdacf5c59efb59cf89a82ad00c87c50c44b7a22")));
    pool_index_map_.insert(std::make_pair(3, common::Encode::HexDecode("807ad978f65021c51034a246101ce420b9d6adea")));
    pool_index_map_.insert(std::make_pair(4, common::Encode::HexDecode("5fba19d135ecf0538e38ac59d9d65c538d4a6995")));
    pool_index_map_.insert(std::make_pair(5, common::Encode::HexDecode("3e70bfe55596e9e782ea210a07e6b4c6d94de3f2")));
    pool_index_map_.insert(std::make_pair(6, common::Encode::HexDecode("3f61e664deb9758ea7f8d5973085f70573f16bf9")));
    pool_index_map_.insert(std::make_pair(7, common::Encode::HexDecode("71387701fc7affbc0994ca8710263aaeab07b57e")));
    pool_index_map_.insert(std::make_pair(8, common::Encode::HexDecode("8155c70d137bbd502af8e387dfcff72be52991f9")));
    pool_index_map_.insert(std::make_pair(9, common::Encode::HexDecode("f729dd8b5cc15a3aa05d3524bd8e284f7d472b02")));
    pool_index_map_.insert(std::make_pair(10, common::Encode::HexDecode("538f48739125c24f80a712bd045e091fa36c67ca")));
    pool_index_map_.insert(std::make_pair(11, common::Encode::HexDecode("37d9c04143f656b85f8f8d89ea017b886871701b")));
    pool_index_map_.insert(std::make_pair(12, common::Encode::HexDecode("c233599bab87018ee37b1231fcf618cfa2fc1b79")));
    pool_index_map_.insert(std::make_pair(13, common::Encode::HexDecode("b04014333eb328947012fcc161fb1ca237a94b19")));
    pool_index_map_.insert(std::make_pair(14, common::Encode::HexDecode("a25f64178db7a9047e9bcbc436418038c64487ee")));
    pool_index_map_.insert(std::make_pair(15, common::Encode::HexDecode("3a62ea86dab4d525358c7a9ee92930ba06737f14")));
    pool_index_map_.insert(std::make_pair(16, common::Encode::HexDecode("66b73fe05b40e6f9a2bb00a067d6fae645675cd8")));
    pool_index_map_.insert(std::make_pair(17, common::Encode::HexDecode("1dedea9fe1aa4b0fc3a7f8553ac8fa431ec98e99")));
    pool_index_map_.insert(std::make_pair(18, common::Encode::HexDecode("26bbb7267d237cfa347067dbfeb1c34e9cfdb81d")));
    pool_index_map_.insert(std::make_pair(19, common::Encode::HexDecode("8663be7cf6a2358551bcb9a6be72d3f7aa855d67")));
    pool_index_map_.insert(std::make_pair(20, common::Encode::HexDecode("910df87a36cc7a6b64fe147af5cc524253c8a9da")));
    pool_index_map_.insert(std::make_pair(21, common::Encode::HexDecode("09d3f21c8c1de7d9ea85d8aba0c2c8a38c7add44")));
    pool_index_map_.insert(std::make_pair(22, common::Encode::HexDecode("bc665df32e84cf17eeba66ab8ef6b81cb475ca18")));
    pool_index_map_.insert(std::make_pair(23, common::Encode::HexDecode("f1f574758fd1f719a9660c0349f1da38a860177f")));
    pool_index_map_.insert(std::make_pair(24, common::Encode::HexDecode("0e07975f02a3e5f01024bfbe71d0a0aebb17249b")));
    pool_index_map_.insert(std::make_pair(25, common::Encode::HexDecode("875787489a792a5bb08e5f5c2bd05a528d331035")));
    pool_index_map_.insert(std::make_pair(26, common::Encode::HexDecode("28443d8a4fb1d2cacbbd960a5f7449650fa3aa9f")));
    pool_index_map_.insert(std::make_pair(27, common::Encode::HexDecode("82c34fa122d992527efe3fbaa84de602b3434d72")));
    pool_index_map_.insert(std::make_pair(28, common::Encode::HexDecode("914609cb8586cfaa94e8382e3c1fb9da4047b83b")));
    pool_index_map_.insert(std::make_pair(29, common::Encode::HexDecode("dbc678c1293867ceb96728ddba7e84168fcaf7b1")));
    pool_index_map_.insert(std::make_pair(30, common::Encode::HexDecode("a4dcdc092fc5e87119cbe4d0a9756ec16e38f800")));
    pool_index_map_.insert(std::make_pair(31, common::Encode::HexDecode("2df3880d3245a1e5f4fef6424eec4edbe2dce9a6")));
    pool_index_map_.insert(std::make_pair(32, common::Encode::HexDecode("c2478e8ecb491ff98f1cbcd00177aca44eeb5b17")));
    pool_index_map_.insert(std::make_pair(33, common::Encode::HexDecode("ba36103c2db7972c216bb3412f6779e0fca4f011")));
    pool_index_map_.insert(std::make_pair(34, common::Encode::HexDecode("23fd31f674384f609b925780c4e43d4886937298")));
    pool_index_map_.insert(std::make_pair(35, common::Encode::HexDecode("cb3542e015d659681540c21ff7c0492f4be9b686")));
    pool_index_map_.insert(std::make_pair(36, common::Encode::HexDecode("8a45971e5858c615e76982699532b0b2e5c14475")));
    pool_index_map_.insert(std::make_pair(37, common::Encode::HexDecode("ffaa813bc136b30b4855576737c12e2adb417da1")));
    pool_index_map_.insert(std::make_pair(38, common::Encode::HexDecode("43d1f9dc1eb11c7d70a97bef79a1960bb9730309")));
    pool_index_map_.insert(std::make_pair(39, common::Encode::HexDecode("9c8bd3c67f03ec201cef875249ede2e72d7bad97")));
    pool_index_map_.insert(std::make_pair(40, common::Encode::HexDecode("684162aa1ccc3f38b989cc8b6b8b8ae22ff13be9")));
    pool_index_map_.insert(std::make_pair(41, common::Encode::HexDecode("d414fe4405e57233f150aa51e27ae801a448bb76")));
    pool_index_map_.insert(std::make_pair(42, common::Encode::HexDecode("e0e7059b16c3ad0b3f5dcd71e45563bfaa26223f")));
    pool_index_map_.insert(std::make_pair(43, common::Encode::HexDecode("6f6b1c32086c6c237170f89793dc558d2bce6324")));
    pool_index_map_.insert(std::make_pair(44, common::Encode::HexDecode("ccd988ab901fc9543e6d5f319e78fa7b7e34e9f5")));
    pool_index_map_.insert(std::make_pair(45, common::Encode::HexDecode("706e86b3ff3d05489b23c7c7d2e795d275be2dd4")));
    pool_index_map_.insert(std::make_pair(46, common::Encode::HexDecode("2e2fa9cfad60352d20ab7290a1c14d160d0a43fc")));
    pool_index_map_.insert(std::make_pair(47, common::Encode::HexDecode("7b7f29b44d0e82741d1d41a9bf96984190c1df75")));
    pool_index_map_.insert(std::make_pair(48, common::Encode::HexDecode("635a41ed2f39e7fc46c994888f17a765b6589aaa")));
    pool_index_map_.insert(std::make_pair(49, common::Encode::HexDecode("85295a25e7818579d85667105a129684fdaa4461")));
    pool_index_map_.insert(std::make_pair(50, common::Encode::HexDecode("7eadfa61a2f6ff5d0b1b7c69a0af2e3950153479")));
    pool_index_map_.insert(std::make_pair(51, common::Encode::HexDecode("fed70c298482a5eedb9caa89dcf653345cfbbbaf")));
    pool_index_map_.insert(std::make_pair(52, common::Encode::HexDecode("733066004f8661a6366201e3e6a5ac6492c9fcd8")));
    pool_index_map_.insert(std::make_pair(53, common::Encode::HexDecode("98a34b5f3c1392c8375e1000c8a87f2bbee452fa")));
    pool_index_map_.insert(std::make_pair(54, common::Encode::HexDecode("c39a9c40f72b4c0825300b010c2b684872a8e3d1")));
    pool_index_map_.insert(std::make_pair(55, common::Encode::HexDecode("fb721640d69a8e43b153356945319d77c24e944a")));
    pool_index_map_.insert(std::make_pair(56, common::Encode::HexDecode("1dc33949b69db1eac17a18f5916e5de7fa45ca40")));
    pool_index_map_.insert(std::make_pair(57, common::Encode::HexDecode("279c0eb9c255a5317d610317c3f99603ae1991c5")));
    pool_index_map_.insert(std::make_pair(58, common::Encode::HexDecode("c57ac699109fbaeb58b5917412ddb83e3fe1ebc0")));
    pool_index_map_.insert(std::make_pair(59, common::Encode::HexDecode("61846e7a889fc32cb43127ab40335bd8a8e486c3")));
    pool_index_map_.insert(std::make_pair(60, common::Encode::HexDecode("3ee58f07088142872d01bd762f721c9ffbeedbd0")));
    pool_index_map_.insert(std::make_pair(61, common::Encode::HexDecode("5fc22f19fda9ad7171e5f38f2db3eec31a2b1306")));
    pool_index_map_.insert(std::make_pair(62, common::Encode::HexDecode("6e242a96e7f73361879eb96f8779c6232d430fc4")));
    pool_index_map_.insert(std::make_pair(63, common::Encode::HexDecode("62896ca4613cec59299152e604dc8ac217b4ed98")));
    pool_index_map_.insert(std::make_pair(64, common::Encode::HexDecode("e0ba6013338157b22fb0a94004140d7c8a82949f")));
    pool_index_map_.insert(std::make_pair(65, common::Encode::HexDecode("6fcbfe3e52a766ca5756e48abd849315680605f0")));
    pool_index_map_.insert(std::make_pair(66, common::Encode::HexDecode("57f1a1e89e7afedcdb32e85dee0f4c0dfb5cfb7b")));
    pool_index_map_.insert(std::make_pair(67, common::Encode::HexDecode("bd3d7b4d3fc0a2d5dca901c6c5e7afbec694cedf")));
    pool_index_map_.insert(std::make_pair(68, common::Encode::HexDecode("6550a9b24e404c1f7a85218bd80a17ec602e991b")));
    pool_index_map_.insert(std::make_pair(69, common::Encode::HexDecode("5cb6414a9d7038d33e2813dfc0d258004cf2d0f4")));
    pool_index_map_.insert(std::make_pair(70, common::Encode::HexDecode("3d34c5047efd09b8322e4b076b1bc8d2ce21a425")));
    pool_index_map_.insert(std::make_pair(71, common::Encode::HexDecode("48c888c7b60fb05c86dffcbea4c4a9fa756b5b5e")));
    pool_index_map_.insert(std::make_pair(72, common::Encode::HexDecode("3310dbbcf8cc7357dec7331906990e033208acd1")));
    pool_index_map_.insert(std::make_pair(73, common::Encode::HexDecode("e480d20bd208f97c2fb9eb20180d0d75930f6727")));
    pool_index_map_.insert(std::make_pair(74, common::Encode::HexDecode("5bd11a6d34039db16742452f82faac0c404e8ac9")));
    pool_index_map_.insert(std::make_pair(75, common::Encode::HexDecode("0ec4b9da4736523fa4397c211679644d4a45bc63")));
    pool_index_map_.insert(std::make_pair(76, common::Encode::HexDecode("ca26aa1ce3e5041d67d8800a2a5934918d51f481")));
    pool_index_map_.insert(std::make_pair(77, common::Encode::HexDecode("11987eab76393bb9e8abfcffe85ec013ac03e2c3")));
    pool_index_map_.insert(std::make_pair(78, common::Encode::HexDecode("ee8f4286ae8d07e9b3d1bde2de73e254b2a82fbb")));
    pool_index_map_.insert(std::make_pair(79, common::Encode::HexDecode("c8a5606b44f45fd3939edd891537e0f6f87e8346")));
    pool_index_map_.insert(std::make_pair(80, common::Encode::HexDecode("a989815c1b486f32eb895bc291891fd363b942c7")));
    pool_index_map_.insert(std::make_pair(81, common::Encode::HexDecode("bd82ee7816d68a87f5019f0394677ae831f864c5")));
    pool_index_map_.insert(std::make_pair(82, common::Encode::HexDecode("8781d32aba9f63c808302df4370aa8951c557a97")));
    pool_index_map_.insert(std::make_pair(83, common::Encode::HexDecode("801d1f42543b7a63a7dae870422dc41cac202ed5")));
    pool_index_map_.insert(std::make_pair(84, common::Encode::HexDecode("2c36ed10d75aff6c39020537abbdfdc00d9bcf80")));
    pool_index_map_.insert(std::make_pair(85, common::Encode::HexDecode("dc2d3796817447b71f22fd84d2a662cf9de3446a")));
    pool_index_map_.insert(std::make_pair(86, common::Encode::HexDecode("2f3c9f22b6867d9deca1e360f186fbcae0ee2bc0")));
    pool_index_map_.insert(std::make_pair(87, common::Encode::HexDecode("a9989e558ae3fbb2fba004246887a7187ddc26a9")));
    pool_index_map_.insert(std::make_pair(88, common::Encode::HexDecode("99dbc5a96a7bd1f2f9fe7bb40fe614ec1de1cae4")));
    pool_index_map_.insert(std::make_pair(89, common::Encode::HexDecode("6e5b97f29286ac2d4b35310d9806bbde990b3d76")));
    pool_index_map_.insert(std::make_pair(90, common::Encode::HexDecode("43bbfdbf2da5b06055608594f344f9ea810a467f")));
    pool_index_map_.insert(std::make_pair(91, common::Encode::HexDecode("1dcd2b53207445673585e69b0e57c7fb91cd5933")));
    pool_index_map_.insert(std::make_pair(92, common::Encode::HexDecode("1f490735f639e216026f3ca9f839d559519be7b3")));
    pool_index_map_.insert(std::make_pair(93, common::Encode::HexDecode("a39b3f1a29f165248d5bb54bad58d98001794a24")));
    pool_index_map_.insert(std::make_pair(94, common::Encode::HexDecode("d836a55b3b9563c2c95879e2149328e3f279bb4b")));
    pool_index_map_.insert(std::make_pair(95, common::Encode::HexDecode("03cc60e1d5b79a6630722c047753678be19d0a6b")));
    pool_index_map_.insert(std::make_pair(96, common::Encode::HexDecode("df7d6fc59b75729d70e8612a962110c70b6606a8")));
    pool_index_map_.insert(std::make_pair(97, common::Encode::HexDecode("b6683bdf237dde5d4ec7d219b45e6e6e39e77a25")));
    pool_index_map_.insert(std::make_pair(98, common::Encode::HexDecode("4a42e0783724805a7d9668f8c219f7b9402aae53")));
    pool_index_map_.insert(std::make_pair(99, common::Encode::HexDecode("e132af1422c58d6c99650b3895502fd055ae6259")));
    pool_index_map_.insert(std::make_pair(100, common::Encode::HexDecode("9faf02f36e3b0941c13d7319e8173c0d2634be4d")));
    pool_index_map_.insert(std::make_pair(101, common::Encode::HexDecode("696a2163e97ddb5158ed6a846aa6ee8ac349957c")));
    pool_index_map_.insert(std::make_pair(102, common::Encode::HexDecode("0b8339e4315e37884e8f66b6c7e83c1ab3a1ff8d")));
    pool_index_map_.insert(std::make_pair(103, common::Encode::HexDecode("4fea1f14ae63a0c9c5fd07ac7de5ff92c9d7604c")));
    pool_index_map_.insert(std::make_pair(104, common::Encode::HexDecode("ee4f8933bc68c6e7ca4073af4ae902097ba27b84")));
    pool_index_map_.insert(std::make_pair(105, common::Encode::HexDecode("b62b5328da9992ea757d65c2d9c2d4f4061fdaa8")));
    pool_index_map_.insert(std::make_pair(106, common::Encode::HexDecode("fff539e383c32ec472afe4c399243d279c64bace")));
    pool_index_map_.insert(std::make_pair(107, common::Encode::HexDecode("fdea67a928a902cb5c8aa9ab083409e8a22a2793")));
    pool_index_map_.insert(std::make_pair(108, common::Encode::HexDecode("e87d12c0bddec14c72b578e254f8dc7784b98610")));
    pool_index_map_.insert(std::make_pair(109, common::Encode::HexDecode("66dfe70b2e9f3b35194e7fd7938a68bd6d26f97a")));
    pool_index_map_.insert(std::make_pair(110, common::Encode::HexDecode("53ca3125f0016cc60c7842841ca92d2da371d385")));
    pool_index_map_.insert(std::make_pair(111, common::Encode::HexDecode("849136472d21aba03860cfbb803dc30ac58feff6")));
    pool_index_map_.insert(std::make_pair(112, common::Encode::HexDecode("a4489081e5c534f5cf9a994ea5d08795505c8127")));
    pool_index_map_.insert(std::make_pair(113, common::Encode::HexDecode("289b26eedc3832986a711a487ecd8dbc44f756ad")));
    pool_index_map_.insert(std::make_pair(114, common::Encode::HexDecode("57ebfcb803873355d4f6089a54abe86b910272cf")));
    pool_index_map_.insert(std::make_pair(115, common::Encode::HexDecode("a37ece6a9967cf198c10862626051cdcbd8a2128")));
    pool_index_map_.insert(std::make_pair(116, common::Encode::HexDecode("166e39c8d8d87af1b54b53eae1232c652b6fb1eb")));
    pool_index_map_.insert(std::make_pair(117, common::Encode::HexDecode("a3114ad894c82d1e5dc922e0dc89db86d01f76ce")));
    pool_index_map_.insert(std::make_pair(118, common::Encode::HexDecode("26fcd7b06fb6dbdc938c0ef7508b7d0c21134ea4")));
    pool_index_map_.insert(std::make_pair(119, common::Encode::HexDecode("c033f40c4b96c0820f429b6dd87879523f373b69")));
    pool_index_map_.insert(std::make_pair(120, common::Encode::HexDecode("28c19bc9aaf0e4e7a86c7e93e8dc6935afecb344")));
    pool_index_map_.insert(std::make_pair(121, common::Encode::HexDecode("b2f0b8e6674b6c998934744905ec28f5afe90991")));
    pool_index_map_.insert(std::make_pair(122, common::Encode::HexDecode("40b4b8cca3755f61d3f074d490fabfdcf83c47ed")));
    pool_index_map_.insert(std::make_pair(123, common::Encode::HexDecode("e35361b65e2cc9f4635561c0de148671cffeacc7")));
    pool_index_map_.insert(std::make_pair(124, common::Encode::HexDecode("02d61e8c35659966317ff9d691c70a2a54b95ea8")));
    pool_index_map_.insert(std::make_pair(125, common::Encode::HexDecode("a355f2023fbec1e75b4c08197541879bfdd076a4")));
    pool_index_map_.insert(std::make_pair(126, common::Encode::HexDecode("6a33053dfc39658d7cd12f23499973ccde54136a")));
    pool_index_map_.insert(std::make_pair(127, common::Encode::HexDecode("d71ceca59dd124b50c9ac7ba8af7f5ca71e51ec4")));
    pool_index_map_.insert(std::make_pair(128, common::Encode::HexDecode("b8c4e7774d823a7823e0d287f230f8190490dff4")));
    pool_index_map_.insert(std::make_pair(129, common::Encode::HexDecode("19a39f25991f37b1fc384a9fe695a592335f696e")));
    pool_index_map_.insert(std::make_pair(130, common::Encode::HexDecode("4bc8026a905be558aa48afd6c4f6b8b797931b81")));
    pool_index_map_.insert(std::make_pair(131, common::Encode::HexDecode("2b3dc2a5f0fffa01583305723b752831fa4496b7")));
    pool_index_map_.insert(std::make_pair(132, common::Encode::HexDecode("fa1a7802fb9ddebc1658126c51727061c9e3d2f3")));
    pool_index_map_.insert(std::make_pair(133, common::Encode::HexDecode("78c18bb0dc6dae13240b36e3e4b49088cf318380")));
    pool_index_map_.insert(std::make_pair(134, common::Encode::HexDecode("0d450ef0b1872c6a168994a2518ed77ca8befd70")));
    pool_index_map_.insert(std::make_pair(135, common::Encode::HexDecode("f516f2508639127aa3de34521725a3c37cc21124")));
    pool_index_map_.insert(std::make_pair(136, common::Encode::HexDecode("19ebe0cd9b3a5a87cf606ddb1688df14fbd655a7")));
    pool_index_map_.insert(std::make_pair(137, common::Encode::HexDecode("a063480636a90c53d0396312964501a87a04d39e")));
    pool_index_map_.insert(std::make_pair(138, common::Encode::HexDecode("324f6f32a97a8bc130c4e69a354ca39db8096ea4")));
    pool_index_map_.insert(std::make_pair(139, common::Encode::HexDecode("92e5906a0f4659d65dc266c66fec0ad5dba37adb")));
    pool_index_map_.insert(std::make_pair(140, common::Encode::HexDecode("d2a0422e82f9c7bc7efd671d331dea940624e766")));
    pool_index_map_.insert(std::make_pair(141, common::Encode::HexDecode("b6db77246a9be607946e6a0b3522cb3471627f65")));
    pool_index_map_.insert(std::make_pair(142, common::Encode::HexDecode("e550642dd131ac9dce8b52b08587a216bd37219b")));
    pool_index_map_.insert(std::make_pair(143, common::Encode::HexDecode("44c13ba0000fc99bccebe03030f35ee8e4ffecb2")));
    pool_index_map_.insert(std::make_pair(144, common::Encode::HexDecode("be5b55ed39452decaffb969010309fba7d73b719")));
    pool_index_map_.insert(std::make_pair(145, common::Encode::HexDecode("bc45c9d95b175e8d44154a73f9ae7a3c24d19599")));
    pool_index_map_.insert(std::make_pair(146, common::Encode::HexDecode("33afe440d08dc7628196ac17229e58adaf73b253")));
    pool_index_map_.insert(std::make_pair(147, common::Encode::HexDecode("098b4221a01c5264fb6bedc69f7d2cd899203a75")));
    pool_index_map_.insert(std::make_pair(148, common::Encode::HexDecode("fad4b8e5a4927d61ea1b697b1575f53077f7aa1a")));
    pool_index_map_.insert(std::make_pair(149, common::Encode::HexDecode("0f0ed4d29bbe6fe2811b5c575821215bfdbc868f")));
    pool_index_map_.insert(std::make_pair(150, common::Encode::HexDecode("24221b3d1a157f54eaebc8072728b580b1c3b4b2")));
    pool_index_map_.insert(std::make_pair(151, common::Encode::HexDecode("66dbe7995980db66844c670c8e605d6be6a3ef1e")));
    pool_index_map_.insert(std::make_pair(152, common::Encode::HexDecode("a0b907107edc7766425de23dc2b1a88d32e5e669")));
    pool_index_map_.insert(std::make_pair(153, common::Encode::HexDecode("f2e14aba57581efe43992d90d8ccf01776d998e7")));
    pool_index_map_.insert(std::make_pair(154, common::Encode::HexDecode("093fefefe1fd7ba031a3dd231163e0a0e12ca36c")));
    pool_index_map_.insert(std::make_pair(155, common::Encode::HexDecode("4d04747e23fd671652b1f9dfbfc28fbf127ebd71")));
    pool_index_map_.insert(std::make_pair(156, common::Encode::HexDecode("71856da1380dc2b962cebca9ee07c30bfedf96a0")));
    pool_index_map_.insert(std::make_pair(157, common::Encode::HexDecode("6a02cb9349170189b677a5874f906cb02e00a6d6")));
    pool_index_map_.insert(std::make_pair(158, common::Encode::HexDecode("4513b5153139fb0dc40dca2990231841017377d5")));
    pool_index_map_.insert(std::make_pair(159, common::Encode::HexDecode("bd4f5985515d1851c5cf4c6bea09e9d3e97b8b71")));
    pool_index_map_.insert(std::make_pair(160, common::Encode::HexDecode("fb643ca732718351d68116cad6e8ecc5ea86faa8")));
    pool_index_map_.insert(std::make_pair(161, common::Encode::HexDecode("6a270ffa71d9cd001808a36d14b9487f2b4ff505")));
    pool_index_map_.insert(std::make_pair(162, common::Encode::HexDecode("cefac77cb6d9d0e0ccb984092b082033dce1ea45")));
    pool_index_map_.insert(std::make_pair(163, common::Encode::HexDecode("552f81537c7278b4a4e9b1bb2abc78863fae86ab")));
    pool_index_map_.insert(std::make_pair(164, common::Encode::HexDecode("a2b02af450f96dee51d1f05d4bc6b875e5402a15")));
    pool_index_map_.insert(std::make_pair(165, common::Encode::HexDecode("5637e0ebbb0678c979f9cac5a24e0320262c61f2")));
    pool_index_map_.insert(std::make_pair(166, common::Encode::HexDecode("659860d4e6e7fb72423353322c4b6a18455d3099")));
    pool_index_map_.insert(std::make_pair(167, common::Encode::HexDecode("bdbe6c93f4881c18c5a80587a1c19e9ffc9ef159")));
    pool_index_map_.insert(std::make_pair(168, common::Encode::HexDecode("7a117062e804897abe12990e56b010bc5bcc073e")));
    pool_index_map_.insert(std::make_pair(169, common::Encode::HexDecode("b0b4245bf08156854b6058a146af355ebcc5e470")));
    pool_index_map_.insert(std::make_pair(170, common::Encode::HexDecode("98dfe4a1be678a283d5c2fddeb75312b88e3b989")));
    pool_index_map_.insert(std::make_pair(171, common::Encode::HexDecode("a33c70905e1567f2c3c62b1be78ab5863fb63416")));
    pool_index_map_.insert(std::make_pair(172, common::Encode::HexDecode("e386ea6dcfa59112cd9b57be205df3275380ae48")));
    pool_index_map_.insert(std::make_pair(173, common::Encode::HexDecode("0c406fc9814e27742246df64c13dbc2549c70295")));
    pool_index_map_.insert(std::make_pair(174, common::Encode::HexDecode("a520d6cd1d11a2834d73828e9fdf7e948180d859")));
    pool_index_map_.insert(std::make_pair(175, common::Encode::HexDecode("43e183c61800de6d79687789065d50dd48a8c61e")));
    pool_index_map_.insert(std::make_pair(176, common::Encode::HexDecode("95f4a0241415e3e70c61fc394b03d600ebcfb683")));
    pool_index_map_.insert(std::make_pair(177, common::Encode::HexDecode("75e618ad36b5b5498a1681ef9ba3f66c44d32f76")));
    pool_index_map_.insert(std::make_pair(178, common::Encode::HexDecode("25cdcfbe5830676272de1ec7007874f39b309e6b")));
    pool_index_map_.insert(std::make_pair(179, common::Encode::HexDecode("e252d01a37b85e2007ed3cc13797aa92496204a4")));
    pool_index_map_.insert(std::make_pair(180, common::Encode::HexDecode("5f15294a1918633d4dd4ec47098a14d01c58e957")));
    pool_index_map_.insert(std::make_pair(181, common::Encode::HexDecode("d45cfd6855c6ec8f635a6f2b46c647e99c59c79d")));
    pool_index_map_.insert(std::make_pair(182, common::Encode::HexDecode("ca929cb8853ec58179831e0e2337a108f6c16a2b")));
    pool_index_map_.insert(std::make_pair(183, common::Encode::HexDecode("8024068e5b8ad09b9672876597361a571492ac32")));
    pool_index_map_.insert(std::make_pair(184, common::Encode::HexDecode("07bc7509bd14d83974d725bb265229fa5f5c48f0")));
    pool_index_map_.insert(std::make_pair(185, common::Encode::HexDecode("0e9a8cf1b0ab7aa81f7192b9cc738a80bae1c446")));
    pool_index_map_.insert(std::make_pair(186, common::Encode::HexDecode("25cec1a782903cf05f98bf0c2bf9da210ef88ae4")));
    pool_index_map_.insert(std::make_pair(187, common::Encode::HexDecode("c423f15e957b1f70a36da35bfb2742713253c1c2")));
    pool_index_map_.insert(std::make_pair(188, common::Encode::HexDecode("c4ddfa4d609e96c9b4f1500dad45b395d62ce55b")));
    pool_index_map_.insert(std::make_pair(189, common::Encode::HexDecode("867e4ee8f7ed63e0ae23bf3aeb54002bda9c98c2")));
    pool_index_map_.insert(std::make_pair(190, common::Encode::HexDecode("4a72cf571a926c3cd1307c03e60d03f12f793a34")));
    pool_index_map_.insert(std::make_pair(191, common::Encode::HexDecode("32b139502fd6c51dd98dbcf02c56e8563573a212")));
    pool_index_map_.insert(std::make_pair(192, common::Encode::HexDecode("bb7a6d239aafe1d43f1067d70ba96c2aefa11588")));
    pool_index_map_.insert(std::make_pair(193, common::Encode::HexDecode("364137f2b252b3420d3e12284303393d0168474a")));
    pool_index_map_.insert(std::make_pair(194, common::Encode::HexDecode("3445c7c121c28d3df097afef6cbc2962c49de854")));
    pool_index_map_.insert(std::make_pair(195, common::Encode::HexDecode("bc3edc4ae46437f9f79ecfcd0090e97477668c97")));
    pool_index_map_.insert(std::make_pair(196, common::Encode::HexDecode("4aaadc02fe2d33d9bcf4bbb2b52d18c96bd71e79")));
    pool_index_map_.insert(std::make_pair(197, common::Encode::HexDecode("9e40328228cbe33156a25d8607b9e7e1dbbe97dc")));
    pool_index_map_.insert(std::make_pair(198, common::Encode::HexDecode("073404d19b1748fd6879a1ba7c90be9ded64f4e5")));
    pool_index_map_.insert(std::make_pair(199, common::Encode::HexDecode("e18139e504c3519226dc9aeecfe0905d610296a6")));
    pool_index_map_.insert(std::make_pair(200, common::Encode::HexDecode("f2bab934e590d7abd454463ca634036e749cc082")));
    pool_index_map_.insert(std::make_pair(201, common::Encode::HexDecode("dc0b45912933abd8df2073860c25b4d7294be93c")));
    pool_index_map_.insert(std::make_pair(202, common::Encode::HexDecode("cd2f08f695a42f24980cfd49f3a7fd5af0124de2")));
    pool_index_map_.insert(std::make_pair(203, common::Encode::HexDecode("7242fb33553cb064065bc9bcd4d2c84bb917d3d8")));
    pool_index_map_.insert(std::make_pair(204, common::Encode::HexDecode("c2adaa8e1a84c911ef2bb614a18311450b7cdcfc")));
    pool_index_map_.insert(std::make_pair(205, common::Encode::HexDecode("ec30ff4f743e4c9d63032268eafb5267b9e6ac47")));
    pool_index_map_.insert(std::make_pair(206, common::Encode::HexDecode("32b217579cb656427f0243c6f445dbfdad870d31")));
    pool_index_map_.insert(std::make_pair(207, common::Encode::HexDecode("a8f26034fb156dfbc41936947f530d2220c61c32")));
    pool_index_map_.insert(std::make_pair(208, common::Encode::HexDecode("957ab27e44bef4b3b451b76fee159422d05ac512")));
    pool_index_map_.insert(std::make_pair(209, common::Encode::HexDecode("9dfc47d1fd695712f7d77bbaa46bba19c77bc031")));
    pool_index_map_.insert(std::make_pair(210, common::Encode::HexDecode("f648548a3372c7997c307a7f9025d0c5c30a32b8")));
    pool_index_map_.insert(std::make_pair(211, common::Encode::HexDecode("c1d0ee8ae2ffea907777916bff0c41fc6c0d3d29")));
    pool_index_map_.insert(std::make_pair(212, common::Encode::HexDecode("e25407100ee4a7cd9f771ca5eeab02754dc7d95d")));
    pool_index_map_.insert(std::make_pair(213, common::Encode::HexDecode("480622bc47a374099037d6657ee08800ee5cd812")));
    pool_index_map_.insert(std::make_pair(214, common::Encode::HexDecode("49db8798a4ba9e0187b7d82105cca9e73cd85add")));
    pool_index_map_.insert(std::make_pair(215, common::Encode::HexDecode("c48b443eeea6b82d2069f939e049bc357785533c")));
    pool_index_map_.insert(std::make_pair(216, common::Encode::HexDecode("287040b9c7c47d141f90a7a4fbf912ab193cedbb")));
    pool_index_map_.insert(std::make_pair(217, common::Encode::HexDecode("6f59f3e44b6590cd6795ceef79921226fe52109b")));
    pool_index_map_.insert(std::make_pair(218, common::Encode::HexDecode("cfcb96df5ad48cc81afada31637f5ecdaa6e9f9c")));
    pool_index_map_.insert(std::make_pair(219, common::Encode::HexDecode("8c3eeec12c1512a05b7b82a9b70e59cebaaac0b5")));
    pool_index_map_.insert(std::make_pair(220, common::Encode::HexDecode("d06e0d77de44dde288020d10a486dcdda3d14f37")));
    pool_index_map_.insert(std::make_pair(221, common::Encode::HexDecode("fb998c36ab68df6e08b7e67479d9ad5ed97dc17c")));
    pool_index_map_.insert(std::make_pair(222, common::Encode::HexDecode("e594ac8ee1416001e7c607827733d48fbf4bf0a0")));
    pool_index_map_.insert(std::make_pair(223, common::Encode::HexDecode("99d880c892fd2f4f452e20ce2c2982bbe5e657ab")));
    pool_index_map_.insert(std::make_pair(224, common::Encode::HexDecode("4ada0f6c3a0c39e76ff7adcd3781fc5f5c8546ca")));
    pool_index_map_.insert(std::make_pair(225, common::Encode::HexDecode("d7ea09043b6c0b529e338c86005446f8d3859bc5")));
    pool_index_map_.insert(std::make_pair(226, common::Encode::HexDecode("7b79d9a09e6b51ea8ab90e733190f23b06e8982f")));
    pool_index_map_.insert(std::make_pair(227, common::Encode::HexDecode("49e32224cd05fc376f4502c1d1b999be1fc66721")));
    pool_index_map_.insert(std::make_pair(228, common::Encode::HexDecode("71aa5939ec3ecbfb325a06fca9c1bd012dcc8d1d")));
    pool_index_map_.insert(std::make_pair(229, common::Encode::HexDecode("c4e481fe56469d6423f198201712b56ddcf7f8e0")));
    pool_index_map_.insert(std::make_pair(230, common::Encode::HexDecode("777a2804e6dc7f42a8fa8cfa93dcab69981aefa3")));
    pool_index_map_.insert(std::make_pair(231, common::Encode::HexDecode("302b21c299836b4be0c0cc96eec435550a8df5c6")));
    pool_index_map_.insert(std::make_pair(232, common::Encode::HexDecode("e6ab924dab162685209399f864b32189ca5c3e11")));
    pool_index_map_.insert(std::make_pair(233, common::Encode::HexDecode("711cd910fe9e3cc63051a8d018140847201ae79a")));
    pool_index_map_.insert(std::make_pair(234, common::Encode::HexDecode("e96a85852053f7701a86f369acff7dddd072ef7a")));
    pool_index_map_.insert(std::make_pair(235, common::Encode::HexDecode("1f1e248325201491d3a88813f48e367b972a4fca")));
    pool_index_map_.insert(std::make_pair(236, common::Encode::HexDecode("5940bd80136a70bf8da933564e1fb6833b1528f1")));
    pool_index_map_.insert(std::make_pair(237, common::Encode::HexDecode("fc023f265148061f43e1390944d666568c12bd67")));
    pool_index_map_.insert(std::make_pair(238, common::Encode::HexDecode("bc664b6b26f3867d92c9f7023ec4187438999ca4")));
    pool_index_map_.insert(std::make_pair(239, common::Encode::HexDecode("ad5fbf4d14230ea29f7ad58115b649e085af5747")));
    pool_index_map_.insert(std::make_pair(240, common::Encode::HexDecode("b84dcc3a6de6621b1643c87cb00980c9e4b12c88")));
    pool_index_map_.insert(std::make_pair(241, common::Encode::HexDecode("bffef2f890309890f7052a0fc0e733359b3bcb87")));
    pool_index_map_.insert(std::make_pair(242, common::Encode::HexDecode("db24d06e5d1ee2ae157b2ad556fc02e61a24a897")));
    pool_index_map_.insert(std::make_pair(243, common::Encode::HexDecode("cfff8eb4f4b5d0a34634cb6675f6558db46d8067")));
    pool_index_map_.insert(std::make_pair(244, common::Encode::HexDecode("3038ab421b6ed4ee44f4f5694f236e7b5b87584f")));
    pool_index_map_.insert(std::make_pair(245, common::Encode::HexDecode("c79cc636c83d700bb3fdad4c81c1887868e168e0")));
    pool_index_map_.insert(std::make_pair(246, common::Encode::HexDecode("a533262d5163e6feb6e1b70ade6d512fadadf0b5")));
    pool_index_map_.insert(std::make_pair(247, common::Encode::HexDecode("ec76475dcebb1d456e9d2fe4ba29b7f16b49f5a9")));
    pool_index_map_.insert(std::make_pair(248, common::Encode::HexDecode("ec0c1181331a7780e1212497cbb39ad2d0057d79")));
    pool_index_map_.insert(std::make_pair(249, common::Encode::HexDecode("f52488bc57e1cfb29db53412f26fd906b9950095")));
    pool_index_map_.insert(std::make_pair(250, common::Encode::HexDecode("635da62f8f21acf7e84db401959628e010e2c8d0")));
    pool_index_map_.insert(std::make_pair(251, common::Encode::HexDecode("c63103ee0604b40b43d64be1b973eb778b3230b8")));
    pool_index_map_.insert(std::make_pair(252, common::Encode::HexDecode("24e358d738594abcf637747e8b7c807ddd560905")));
    pool_index_map_.insert(std::make_pair(253, common::Encode::HexDecode("d9ec5aff3001dece14e1f4a35a39ed506bd6274a")));
    pool_index_map_.insert(std::make_pair(254, common::Encode::HexDecode("e70c72fcdb57df6844e4c44cd9f02435b628398c")));
    pool_index_map_.insert(std::make_pair(255, common::Encode::HexDecode("db1e0168d3da18ec513ea4c0ebc7ca000563df68")));

}

void GenesisBlockInit::GenerateRootAccounts() {
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
// 
//     std::map<std::string, std::string> pri_pub_map;
//     while (root_account_with_pool_index_map_.size() < common::kImmutablePoolSize) {
//         security::PrivateKey prikey;
//         security::PublicKey pubkey(prikey);
//         std::string pubkey_str;
//         pubkey.Serialize(pubkey_str, false);
//         std::string prikey_str;
//         prikey.Serialize(prikey_str);
// 
//         std::string address = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
//         std::string address1 = security::Secp256k1::Instance()->ToAddressWithPrivateKey(prikey_str);
//         assert(address == address1);
//         auto pool_index = common::GetBasePoolIndex(address);
//         auto iter = root_account_with_pool_index_map_.find(pool_index);
//         if (iter != root_account_with_pool_index_map_.end()) {
//             continue;
//         }
// 
//         root_account_with_pool_index_map_.insert(std::make_pair(pool_index, address));
//         pri_pub_map[address] = prikey_str;
//     }
// 
//     for (auto iter = pool_index_map_.begin(); iter != pool_index_map_.end(); ++iter) {
//         std::cout << common::Encode::HexEncode(iter->second) << "\t" << common::Encode::HexEncode(pri_pub_map[iter->second]) << std::endl;
//     }
// 
//     for (auto iter = root_account_with_pool_index_map_.begin(); iter != root_account_with_pool_index_map_.end(); ++iter) {
//         std::cout << "root_account_with_pool_index_map_.insert(std::make_pair(" << iter->first << ", common::Encode::HexDecode(\"" << common::Encode::HexEncode(iter->second) << "\")));" << std::endl;
//    }
}

};  // namespace init

};  // namespace zjchain
