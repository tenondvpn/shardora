#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <gtest/gtest.h>

#define private public
#define protected public
#include "block/bft_utils.h"
#include "bls/bls_sign.h"
#include "bls/bls_dkg.h"
#include "election/proto/elect.pb.h"
#include "ck/ck_client.h"
#include "dht/base_dht.h"
#include "dht/dht_utils.h"
#include "protos/block.pb.h"

namespace zjchain {

namespace ck {

namespace test {

class TestCk : public testing::Test {
public:
    static void SetUpTestCase() {      
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void CreateBlsGenesisKeys(
            std::vector<std::shared_ptr<BLSPrivateKeyShare>>* skeys,
            std::vector<std::shared_ptr<BLSPublicKeyShare>>* pkeys,
            libff::alt_bn128_G2* common_public_key) {
        static const uint32_t t = 2;
        static const uint32_t n = 3;
        libBLS::Dkg dkg_instance = libBLS::Dkg(t, n);
        std::vector<std::vector<libff::alt_bn128_Fr>> polynomial(n);
        for (auto& pol : polynomial) {
            pol = dkg_instance.GeneratePolynomial();
        }

        std::vector<std::vector<libff::alt_bn128_Fr>> secret_key_contribution(n);
        for (size_t i = 0; i < n; ++i) {
            secret_key_contribution[i] = dkg_instance.SecretKeyContribution(polynomial[i]);
        }

        std::vector<std::vector<libff::alt_bn128_G2>> verification_vector(n);
        for (size_t i = 0; i < n; ++i) {
            verification_vector[i] = dkg_instance.VerificationVector(polynomial[i]);
        }

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i; j < n; ++j) {
                std::swap(secret_key_contribution[j][i], secret_key_contribution[i][j]);
            }
        }

        *common_public_key = libff::alt_bn128_G2::zero();
        for (size_t i = 0; i < n; ++i) {
            *common_public_key = *common_public_key + polynomial[i][0] * libff::alt_bn128_G2::one();
            BLSPrivateKeyShare cur_skey(
                dkg_instance.SecretKeyShareCreate(secret_key_contribution[i]), t, n);
            skeys->push_back(std::make_shared<BLSPrivateKeyShare>(cur_skey));
            BLSPublicKeyShare pkey(*cur_skey.getPrivateKey(), t, n);
            pkeys->push_back(std::make_shared<BLSPublicKeyShare>(pkey));
        }
    }

    std::shared_ptr<block::protobuf::Block> CreateElectBlock(
            uint32_t shard_netid,
            std::string& root_pre_hash,
            uint64_t height,
            uint64_t prev_height,
            const std::vector<dht::NodePtr>& genesis_nodes) {
        auto zjc_block = std::make_shared<block::protobuf::Block>();
        auto tx_list = zjc_block->mutable_tx_list();
        auto tx_info = tx_list->Add();
        tx_info->set_type(1);
        tx_info->set_from("from");
        tx_info->set_version(1);
        tx_info->set_amount(0);
        tx_info->set_gas_limit(0);
        tx_info->set_gas_used(0);
        tx_info->set_balance(0);
        tx_info->set_status(0);
        tx_info->set_network_id(shard_netid);
        elect::protobuf::ElectBlock ec_block;
        int32_t expect_leader_count = (int32_t)pow(2.0, (double)((int32_t)log2(double(genesis_nodes.size() / 3))));
        int32_t node_idx = 0;
        for (auto iter = genesis_nodes.begin(); iter != genesis_nodes.end(); ++iter) {
            auto in = ec_block.add_in();
            in->set_pubkey((*iter)->pubkey_str());
            in->set_pool_idx_mod_num(node_idx < expect_leader_count ? node_idx : -1);
            ++node_idx;
        }

        ec_block.set_shard_network_id(shard_netid);
        if (prev_height != common::kInvalidUint64) {
            std::vector<std::shared_ptr<BLSPrivateKeyShare>> skeys;
            std::vector<std::shared_ptr<BLSPublicKeyShare>> pkeys;
            libff::alt_bn128_G2 common_public_key;
            CreateBlsGenesisKeys(&skeys, &pkeys, &common_public_key);
            auto prev_members = ec_block.mutable_prev_members();
            for (uint32_t i = 0; i < genesis_nodes.size(); ++i) {
                auto mem_pk = prev_members->add_bls_pubkey();
                auto pkeys_str = pkeys[i]->toString();
                mem_pk->set_x_c0(pkeys_str->at(0));
                mem_pk->set_x_c1(pkeys_str->at(1));
                mem_pk->set_y_c0(pkeys_str->at(2));
                mem_pk->set_y_c1(pkeys_str->at(3));
                if (i == 0) {
                    mem_pk->set_pool_idx_mod_num(0);
                } else {
                    mem_pk->set_pool_idx_mod_num(-1);
                }
            }

            auto common_pk_ptr = std::make_shared<BLSPublicKey>(common_public_key);
            auto common_pk_strs = common_pk_ptr->toString();
            auto common_pk = prev_members->mutable_common_pubkey();
            common_pk->set_x_c0(common_pk_strs->at(0));
            common_pk->set_x_c1(common_pk_strs->at(1));
            common_pk->set_y_c0(common_pk_strs->at(2));
            common_pk->set_y_c1(common_pk_strs->at(3));
            prev_members->set_prev_elect_height(prev_height);
        }

        auto ec_block_attr = tx_info->add_attr();
        ec_block_attr->set_key("key");
        ec_block_attr->set_value(ec_block.SerializeAsString());
        zjc_block->set_prehash(root_pre_hash);
        zjc_block->set_version(common::kTransactionVersion);
        zjc_block->set_pool_index(common::kRootChainPoolIndex);
        zjc_block->set_height(height);
        common::Bitmap root_bitmap_(1024);
        for (uint32_t i = 0; i < 1024; ++i) {
            root_bitmap_.Set(i);
        }

        const auto& bitmap_data = root_bitmap_.data();
        for (uint32_t i = 0; i < bitmap_data.size(); ++i) {
            zjc_block->add_bitmap(bitmap_data[i]);
        }

        zjc_block->set_network_id(2);
        zjc_block->set_hash(block::GetBlockHash(*zjc_block));
        return zjc_block;
    }
};

TEST_F(TestCk, All) {
    ClickHouseClient ck_client("127.0.0.1", "", "");
    ASSERT_TRUE(ck_client.CreateTable());
    std::vector<dht::NodePtr> root_genesis_nodes;
    std::string value = "031d29587f946b7e57533725856e3b2fc840ac8395311fea149642334629cd5757:127.0.0.1:1,03a6f3b7a4a3b546d515bfa643fc4153b86464543a13ab5dd05ce6f095efb98d87:127.0.0.1:2,031e886027cdf3e7c58b9e47e8aac3fe67c393a155d79a96a0572dd2163b4186f0:127.0.0.1:2";
    common::Split<2048> nodes_split(value.c_str(), ',', value.size());
    for (uint32_t i = 0; i < nodes_split.Count(); ++i) {
        common::Split<> node_info(nodes_split[i], ':', nodes_split.SubLen(i));
        if (node_info.Count() != 3) {
            continue;
        }

        auto node_ptr = std::make_shared<dht::Node>();
        node_ptr->set_pubkey(common::Encode::HexDecode(node_info[0]));
        node_ptr->set_public_ip(node_info[1]);
        if (!common::StringUtil::ToUint16(node_info[2], &node_ptr->public_port)) {
            continue;
        }

        root_genesis_nodes.push_back(node_ptr);
    }
    std::string root_pre_hash = "pre_hash";
    auto block_ptr = CreateElectBlock(2, root_pre_hash, 1, 0, root_genesis_nodes);
    ASSERT_TRUE(block_ptr != nullptr);
    ASSERT_TRUE(ck_client.AddNewBlock(block_ptr));
    std::string cmd = std::string("select * from ") + kClickhouseTableName + (" where shard_id=2 limit 0, 10");
    ck_client.client_.Select(cmd, [](const clickhouse::Block& ck_block) {
        for (size_t c = 0; c < ck_block.GetRowCount(); ++c) {
            std::cout << "shard id: " << (*ck_block[0]->As<clickhouse::ColumnUInt32>())[c] << std::endl;
            std::cout << "pool_index: " << (*ck_block[1]->As<clickhouse::ColumnUInt32>())[c] << std::endl;
            std::cout << "height: " << (*ck_block[2]->As<clickhouse::ColumnUInt64>())[c] << std::endl;
        }
    });
}

}  // namespace test

}  // namespace ck

}  // namespace zjchain
