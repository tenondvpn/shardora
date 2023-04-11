#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <gtest/gtest.h>

#include "bzlib.h"

#include "common/random.h"
#include "common/hash.h"
#include "db/db.h"
#include "dht/dht_key.h"
#include "dht/base_dht.h"
#include "network/dht_manager.h"
#include "network/universal_manager.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/multi_thread.h"
#include "transport/tcp_transport.h"
#include "transport/transport_utils.h"
#define private public
#include "bls/bls_sign.h"
#include "bls/bls_dkg.h"
#include "bls/bls_manager.h"
#include "protos/bls.pb.h"
#include "network/network_utils.h"

namespace zjchain {

namespace bls {

namespace test {

static std::shared_ptr<security::Security> security_ptr = nullptr;
static BlsManager* bls_manager = nullptr;
static std::shared_ptr<db::Db> db_ptr = nullptr;

class TestBls : public testing::Test {
public:
    static void SetUpTestCase() {
        std::string config_path_ = "./";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/zjc.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);
        system("rm -rf ./db");
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("./db");
        bls_manager = new BlsManager(security_ptr, db_ptr);
    }

    static void WriteDefaultLogConf(
        const std::string& log_conf_path,
        const std::string& log_path) {
        FILE* file = NULL;
        file = fopen(log_conf_path.c_str(), "w");
        if (file == NULL) {
            return;
        }
        std::string log_str = ("# log4cpp.properties\n"
            "log4cpp.rootCategory = DEBUG\n"
            "log4cpp.category.sub1 = DEBUG, programLog\n"
            "log4cpp.appender.rootAppender = ConsoleAppender\n"
            "log4cpp.appender.rootAppender.layout = PatternLayout\n"
            "log4cpp.appender.rootAppender.layout.ConversionPattern = %d [%p] %m%n\n"
            "log4cpp.appender.programLog = RollingFileAppender\n"
            "log4cpp.appender.programLog.fileName = ") + log_path + "\n" +
            std::string("log4cpp.appender.programLog.maxFileSize = 1073741824\n"
                "log4cpp.appender.programLog.maxBackupIndex = 1\n"
                "log4cpp.appender.programLog.layout = PatternLayout\n"
                "log4cpp.appender.programLog.layout.ConversionPattern = %d [%p] %m%n\n");
        fwrite(log_str.c_str(), log_str.size(), 1, file);
        fclose(file);
    }

    static void TearDownTestCase() {
//         transport_->Stop();
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    static void SetGloableInfo(const std::string& private_key, uint32_t network_id) {
        security_ptr = std::make_shared<security::Ecdsa>();
        security_ptr->SetPrivateKey(common::Encode::HexDecode(
            "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971"));
        std::string pubkey_str;
        std::string id = security_ptr->GetAddress();
        common::GlobalInfo::Instance()->set_network_id(network_id);
        JoinNetwork(network::kRootCongressNetworkId);
        JoinNetwork(network::kUniversalNetworkId);
        JoinNetwork(network::kConsensusShardBeginNetworkId);
    }

    static void JoinNetwork(uint32_t network_id) {
        if (network_id > 1) {
            network::DhtManager::Instance()->UnRegisterDht(network_id);
        }

        if (network_id <= 1) {
            network::UniversalManager::Instance()->UnRegisterUniversal(network_id);
        }

        dht::DhtKeyManager dht_key(
            network_id,
            security_ptr->GetAddress());
        dht::NodePtr local_node = std::make_shared<dht::Node>(
            network_id,
            common::GlobalInfo::Instance()->config_local_ip(),
            common::GlobalInfo::Instance()->config_local_port(),
            security_ptr->GetPublicKey(),
            security_ptr->GetAddress());
        local_node->first_node = true;
        auto dht = std::make_shared<dht::BaseDht>(local_node);
        dht->Init(security_ptr, nullptr, nullptr);
        auto base_dht = std::dynamic_pointer_cast<dht::BaseDht>(dht);
        if (network_id > 1) {
            network::DhtManager::Instance()->RegisterDht(network_id, base_dht);
        }

        if (network_id <= 1) {
            network::UniversalManager::Instance()->RegisterUniversal(network_id, base_dht);
        }
    }
};

TEST_F(TestBls, ContributionSignAndVerify) {
    static const uint32_t t = 700;
    static const uint32_t n = 1024;
    static const uint32_t valid_t = 2;
    static const uint32_t valid_count = 3;

    SetGloableInfo(common::Random::RandomString(32), network::kConsensusShardBeginNetworkId);
    BlsDkg* dkg = new BlsDkg[n];
    auto btime0 = common::TimeUtils::TimestampUs();
    for (uint32_t i = 0; i < valid_count; i++) {
        dkg[i].Init(
            bls_manager,
            security_ptr,
            valid_t,
            valid_count,
            libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(),
            libff::alt_bn128_G2::zero(),
            db_ptr);
        dkg[i].dkg_instance_ = std::make_shared<libBLS::Dkg>(t, n);
        dkg[i].local_member_index_ = i;
        dkg[i].CreateContribution(n, valid_t);
    }

    std::cout << "CreateContribution time us: " << (common::TimeUtils::TimestampUs() - btime0) << std::endl;
    libff::alt_bn128_G2 test_valid_public_key;
    std::vector<libff::alt_bn128_Fr> test_valid_seck_keys;
    libff::alt_bn128_Fr test_valid_seck_key;
    for (uint32_t i = 0; i < valid_count; ++i) {
        for (uint32_t j = i; j < valid_count; ++j) {
            std::swap(
                dkg[i].local_src_secret_key_contribution_[j],
                dkg[j].local_src_secret_key_contribution_[i]);
        }
    }

    for (uint32_t i = 0; i < valid_count; ++i) {
        for (uint32_t j = valid_count; j < n; ++j) {
            dkg[i].local_src_secret_key_contribution_[j] = libff::alt_bn128_Fr::zero();
        }
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec(valid_t);
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);
    auto common_public_key = libff::alt_bn128_G2::zero();
    auto btime1 = common::TimeUtils::TimestampUs();
    for (uint32_t i = 0; i < valid_count; ++i) {
        common_public_key = common_public_key + dkg[i].g2_vec_;
        libBLS::Dkg tmpdkg(valid_t, n);
        dkg[i].local_sec_key_ = tmpdkg.SecretKeyShareCreate(dkg[i].local_src_secret_key_contribution_);
        dkg[i].local_publick_key_ = tmpdkg.GetPublicKeyFromSecretKey(dkg[i].local_sec_key_);
        libff::alt_bn128_G1 sign;
        bls_sign.Sign(valid_t, n, dkg[i].local_sec_key_, hash, &sign);
        std::string verify_hash;
        // slow
        ASSERT_EQ(
            bls_sign.Verify(valid_t, n, sign, hash, dkg[i].local_publick_key_, &verify_hash),
            kBlsSuccess);
        if (i < valid_t) {
            all_signs.push_back(sign);
            idx_vec[i] = i + 1;
        }
    }

    std::cout << "sign time us: " << (common::TimeUtils::TimestampUs() - btime1) << std::endl;
    auto t0 = common::TimeUtils::TimestampUs();
    libBLS::Bls bls_instance = libBLS::Bls(valid_t, n);
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, valid_t);
    std::cout << "LagrangeCoeffs use time us: " << (common::TimeUtils::TimestampUs() - t0) << std::endl;
    auto t1 = common::TimeUtils::TimestampUs();
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);
    std::cout << "SignatureRecover use time us: " << (common::TimeUtils::TimestampUs() - t1) << std::endl;
    std::string verify_hash;
    // slow
    auto t2 = common::TimeUtils::TimestampUs();
    ASSERT_EQ(
        bls_sign.Verify(valid_t, n, agg_sign, hash, common_public_key, &verify_hash),
        kBlsSuccess);
    std::cout << "use time us: " << (common::TimeUtils::TimestampUs() - t2) << std::endl;
}

static void GetPrivateKey(std::vector<std::string>& pri_vec, uint32_t n) {
    FILE* prikey_fd = fopen("prikey", "r");
    if (prikey_fd != nullptr) {
        char line[128];
        while (!feof(prikey_fd)) {
            fgets(line, 128, prikey_fd);
            pri_vec.push_back(common::Encode::HexDecode(std::string(line, 64)));
        }

        fclose(prikey_fd);
    }

    if (pri_vec.empty()) {
        FILE* prikey_fd = fopen("prikey", "w");
        for (uint32_t i = 0; i < n; ++i) {
            pri_vec.push_back(common::Random::RandomString(32));
            std::string val = common::Encode::HexEncode(pri_vec[i]) + "\n";
            fwrite(val.c_str(), 1, val.size(), prikey_fd);
        }

        fclose(prikey_fd);
    }
}

static void CreateContribution(
        common::MembersPtr members,
        BlsDkg* dkg,
        const std::vector<std::string>& pri_vec,
        std::shared_ptr<TimeBlockItem>& latest_timeblock_info,
        std::vector<transport::MessagePtr>& verify_brd_msgs) {
    FILE* rlocal_bls_fd = fopen("local_bls", "r");
    bool exists = false;
    if (rlocal_bls_fd != nullptr) {
        char* line = new char[1024 * 1024];
        uint32_t idx = 0;
        while (!feof(rlocal_bls_fd)) {
            fgets(line, 1024 * 1024, rlocal_bls_fd);
            std::string val = common::Encode::HexDecode(std::string(line, strlen(line) - 1));
            bls::protobuf::LocalBlsItem local_item;
            ASSERT_TRUE(local_item.ParseFromString(val));
            dkg[idx].prefix_db_->SaveBlsInfo(dkg[idx].security_, local_item);
        }

        exists = true;
        fclose(rlocal_bls_fd);
    }

    FILE* local_bls_fd = nullptr;
    if (!exists) {
        local_bls_fd = fopen("local_bls", "w");
    }

    for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        //         SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].OnNewElectionBlock(1, members, latest_timeblock_info);
        dkg[i].local_member_index_ = i;
        dkg[i].BroadcastVerfify(0);
        verify_brd_msgs.push_back(dkg[i].ver_brd_msg_);
        ASSERT_EQ(dkg[i].ver_brd_msg_->header.bls_proto().elect_height(), 1);
        dkg[i].ver_brd_msg_ = nullptr;
        if (!exists) {
            bls::protobuf::LocalBlsItem local_item;
            ASSERT_TRUE(dkg[i].prefix_db_->GetBlsInfo(dkg[i].security_, &local_item));
            std::string val = common::Encode::HexEncode(local_item.SerializeAsString()) + "\n";
            fwrite(val.c_str(), 1, val.size(), local_bls_fd);
        }
    }

    if (!exists) {
        fclose(local_bls_fd);
    }
}

TEST_F(TestBls, AllSuccess) {
//     static const uint32_t t = 700;
//     static const uint32_t n = 1024;
    static const uint32_t n = 1024;
    static const uint32_t t = common::GetSignerCount(n);

    BlsDkg* dkg = new BlsDkg[n];
    for (uint32_t i = 0; i < n; i++) {
        dkg[i].Init(
            bls_manager,
            security_ptr,
            t,
            n,
            libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(),
            libff::alt_bn128_G2::zero(),
            db_ptr);
    }

    common::MembersPtr members = std::make_shared<common::Members>();
    std::vector<std::string> pri_vec;
    GetPrivateKey(pri_vec, n);
    for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        security::Ecdsa ecdsa;
        ecdsa.SetPrivateKey(pri_vec[i]);
        std::string pubkey_str = ecdsa.GetPublicKey();
        std::string id = ecdsa.GetAddress();
        auto member = std::make_shared<common::BftMember>(
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, 0, i == 0 ? 0 : -1);
        member->public_ip = common::IpToUint32("127.0.0.1");
        member->public_port = 123;
        members->push_back(member);
    }

    auto time0 = common::TimeUtils::TimestampUs();
    std::vector<transport::MessagePtr> verify_brd_msgs;
    auto latest_timeblock_info = std::make_shared<TimeBlockItem>();
    latest_timeblock_info->lastest_time_block_tm = common::TimeUtils::TimestampSeconds() - 10;
    latest_timeblock_info->latest_time_block_height = 1;
    latest_timeblock_info->vss_random = common::Random::RandomUint64();
    CreateContribution(members, dkg, pri_vec, latest_timeblock_info, verify_brd_msgs);
    auto time1 = common::TimeUtils::TimestampUs();
    std::cout << "0: " << (time1 - time0) << std::endl;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = verify_brd_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
    }

    verify_brd_msgs.swap(std::vector<transport::MessagePtr>());
    auto time2 = common::TimeUtils::TimestampUs();
    std::cout << "1: " << (time2 - time1) << std::endl;
    // swap sec key
    std::vector<transport::MessagePtr> swap_sec_msgs;
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].SwapSecKey(0);
        swap_sec_msgs.push_back(dkg[i].sec_swap_msgs_);
    }

    auto time3 = common::TimeUtils::TimestampUs();
    std::cout << "2: " << (time3 - time2) << std::endl;
    for (uint32_t i = 0; i < n; ++i) {
        auto t0 = common::TimeUtils::TimestampUs();
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = swap_sec_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
        auto t1 = common::TimeUtils::TimestampUs();
    }

    swap_sec_msgs.swap(std::vector<transport::MessagePtr>());
    auto time4 = common::TimeUtils::TimestampUs();
    std::cout << "3: " << (time4 - time3) << std::endl;
    std::cout << "success handle bls test message." << std::endl;
    // sign and verify
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);
    std::vector<libff::alt_bn128_G1> all_signs;
    for (uint32_t i = 0; i < n; ++i) {
        dkg[i].FinishNoLock(0);
        ASSERT_TRUE(dkg[i].finished_);
    }

    std::vector<size_t> idx_vec(t);
    for (size_t i = 0; i < t; ++i) {
        BlsSign bls_sign;
        libff::alt_bn128_G1 sign;
        bls_sign.Sign(t, n, dkg[i].local_sec_key_, hash, &sign);
        std::string verify_hash;
        // slow
        ASSERT_EQ(
            bls_sign.Verify(t, n, sign, hash, dkg[i].local_publick_key_, &verify_hash),
            kBlsSuccess);
        all_signs.push_back(sign);
        idx_vec[i] = i + 1;
    }

    auto time5 = common::TimeUtils::TimestampUs();
    std::cout << "4: " << (time5 - time4) << std::endl;
    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);
    auto time6 = common::TimeUtils::TimestampUs();
    std::cout << "5: " << (time6 - time5) << std::endl;
    for (uint32_t i = 0; i < n; ++i) {
        BlsSign bls_sign;
        std::string verify_hash;
        // slow
        ASSERT_EQ(
            bls_sign.Verify(t, n, agg_sign, hash, dkg[i].common_public_key_, &verify_hash),
            kBlsSuccess);
    }

    auto time7 = common::TimeUtils::TimestampUs();
    std::cout << "6: " << (time7 - time6) << std::endl;
}

TEST_F(TestBls, FinishWithMissingNodesNoVerify) {
    // t = 7, n = 10
    static uint32_t t = 7;
    static uint32_t n = 10;

    BlsDkg dkg[n];
    for (uint32_t i = 0; i < n; i++) {
        dkg[i].Init(
            bls_manager, security_ptr, 0, 0, libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(), libff::alt_bn128_G2::zero(), db_ptr);
    }
    common::MembersPtr members = std::make_shared<common::Members>();
    std::vector<std::string> pri_vec;
    for (uint32_t i = 0; i < n; ++i) {
        pri_vec.push_back(common::Random::RandomString(32));
    }

    for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        security::Ecdsa ecdsa;
        ecdsa.SetPrivateKey(pri_vec[i]);
        std::string pubkey_str = ecdsa.GetPublicKey();
        std::string id = ecdsa.GetAddress();
        auto member = std::make_shared<common::BftMember>(
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, 0, i == 0 ? 0 : -1);
        member->public_ip = common::IpToUint32("127.0.0.1");
        member->public_port = 123;
        members->push_back(member);
    }

    std::vector<transport::MessagePtr> verify_brd_msgs;
    auto latest_timeblock_info = std::make_shared<TimeBlockItem>();
    latest_timeblock_info->lastest_time_block_tm = common::TimeUtils::TimestampSeconds() - 10;
    latest_timeblock_info->latest_time_block_height = 1;
    latest_timeblock_info->vss_random = common::Random::RandomUint64();
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].OnNewElectionBlock(1, members, latest_timeblock_info);
        dkg[i].local_member_index_ = i;
        dkg[i].BroadcastVerfify(0);
        verify_brd_msgs.push_back(dkg[i].ver_brd_msg_);
    }

    static const uint32_t kInvalidNodeIndex = 5;
    static const uint32_t kInvalidSwapNodeIndex = 3;
    static const uint32_t kInvalidSwapNodeIndex2 = 4;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = verify_brd_msgs[i];
            msg_ptr->thread_idx = 0;
            if (i == kInvalidNodeIndex) {
                continue;
            }

            dkg[j].HandleMessage(msg_ptr);
        }
    }

    // swap sec key
    std::vector<transport::MessagePtr> swap_sec_msgs;
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].SwapSecKey(0);
        swap_sec_msgs.push_back(dkg[i].sec_swap_msgs_);
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = swap_sec_msgs[i];
            msg_ptr->thread_idx = 0;
            if (i == kInvalidSwapNodeIndex && j == kInvalidSwapNodeIndex2) {
                continue;
            }

            dkg[j].HandleMessage(msg_ptr);
        }
    }

    // sign and verify
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);
    for (uint32_t i = 0; i < n; ++i) {
        dkg[i].FinishNoLock(0);
        if (i != kInvalidNodeIndex) {
            ASSERT_TRUE(dkg[i].finished_);
        }
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec(t);
    for (size_t i = 0; i < n; ++i) {
        if (i == kInvalidNodeIndex || i == kInvalidSwapNodeIndex || i == kInvalidSwapNodeIndex2) {
            continue;
        }

        BlsSign bls_sign;
        libff::alt_bn128_G1 sign;
        bls_sign.Sign(t, n, dkg[i].local_sec_key_, hash, &sign);
        std::string verify_hash;
        ASSERT_EQ(
            bls_sign.Verify(t, n, sign, hash, dkg[i].local_publick_key_, &verify_hash),
            kBlsSuccess);
        all_signs.push_back(sign);
        idx_vec[all_signs.size() - 1] = i + 1;
        if (all_signs.size() >= t) {
            break;
        }
    }

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);
    for (uint32_t i = 0; i < n; ++i) {
        if (i == kInvalidNodeIndex || i == kInvalidSwapNodeIndex2) {
            continue;
        }

        EXPECT_EQ(dkg[i].common_public_key_, dkg[0].common_public_key_);
        BlsSign bls_sign;
        std::string verify_hash;
        EXPECT_EQ(
            bls_sign.Verify(t, n, agg_sign, hash, dkg[i].common_public_key_, &verify_hash),
            kBlsSuccess);
    }
}

TEST_F(TestBls, FinishWithMissingNodesNoVerify5) {
    // t = 7, n = 10
    static uint32_t t = 4;
    static uint32_t n = 5;

    BlsDkg dkg[n];
    for (uint32_t i = 0; i < n; i++) {
        dkg[i].Init(bls_manager, security_ptr, 0, 0, libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(), libff::alt_bn128_G2::zero(), db_ptr);
    }
    common::MembersPtr members = std::make_shared<common::Members>();
    std::vector<std::string> pri_vec;
    for (uint32_t i = 0; i < n; ++i) {
        pri_vec.push_back(common::Random::RandomString(32));
    }

    for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        security::Ecdsa ecdsa;
        ecdsa.SetPrivateKey(pri_vec[i]);
        std::string pubkey_str = ecdsa.GetPublicKey();
        std::string id = ecdsa.GetAddress();
        auto member = std::make_shared<common::BftMember>(
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, 0, i == 0 ? 0 : -1);
        member->public_ip = common::IpToUint32("127.0.0.1");
        member->public_port = 123;
        members->push_back(member);
    }

    std::vector<transport::MessagePtr> verify_brd_msgs;
    auto latest_timeblock_info = std::make_shared<TimeBlockItem>();
    latest_timeblock_info->lastest_time_block_tm = common::TimeUtils::TimestampSeconds() - 10;
    latest_timeblock_info->latest_time_block_height = 1;
    latest_timeblock_info->vss_random = common::Random::RandomUint64();
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].OnNewElectionBlock(1, members, latest_timeblock_info);
        dkg[i].local_member_index_ = i;
        dkg[i].BroadcastVerfify(0);
        verify_brd_msgs.push_back(dkg[i].ver_brd_msg_);
    }

    static const uint32_t kInvalidNodeIndex = 3;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = verify_brd_msgs[i];
            msg_ptr->thread_idx = 0;
            if (i == kInvalidNodeIndex) {
                continue;
            }

            dkg[j].HandleMessage(msg_ptr);
        }
    }

    // swap sec key
    std::vector<transport::MessagePtr> swap_sec_msgs;
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].SwapSecKey(0);
        swap_sec_msgs.push_back(dkg[i].sec_swap_msgs_);
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = swap_sec_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
    }

    // sign and verify
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);
    for (uint32_t i = 0; i < n; ++i) {
        dkg[i].FinishNoLock(0);
        if (i != kInvalidNodeIndex) {
            ASSERT_TRUE(dkg[i].finished_);
        }
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec(t);
    for (size_t i = 0; i < n; ++i) {
        if (i == kInvalidNodeIndex) {
            continue;
        }

        BlsSign bls_sign;
        libff::alt_bn128_G1 sign;
        bls_sign.Sign(t, n, dkg[i].local_sec_key_, hash, &sign);
        std::string verify_hash;
        ASSERT_EQ(
            bls_sign.Verify(t, n, sign, hash, dkg[i].local_publick_key_, &verify_hash),
            kBlsSuccess);
        all_signs.push_back(sign);
        idx_vec[all_signs.size() - 1] = i + 1;
        if (all_signs.size() >= t) {
            break;
        }
    }

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);
    for (uint32_t i = 0; i < n; ++i) {
        if (i == kInvalidNodeIndex) {
            continue;
        }

        BlsSign bls_sign;
        std::string verify_hash;
        EXPECT_EQ(
            bls_sign.Verify(t, n, agg_sign, hash, dkg[i].common_public_key_, &verify_hash),
            kBlsSuccess);
    }
}

TEST_F(TestBls, ThreeRatioFailFine) {
    // t = 7, n = 10
    static const uint32_t t = 7;
    static const uint32_t n = 10;

    BlsDkg dkg[n];
    for (uint32_t i = 0; i < n; i++) {
        dkg[i].Init(bls_manager, security_ptr, 0, 0, libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(), libff::alt_bn128_G2::zero(), db_ptr);
    }
    common::MembersPtr members = std::make_shared<common::Members>();
    std::vector<std::string> pri_vec;
    for (uint32_t i = 0; i < n; ++i) {
        pri_vec.push_back(common::Random::RandomString(32));
    }

    for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        security::Ecdsa ecdsa;
        ecdsa.SetPrivateKey(pri_vec[i]);
        std::string pubkey_str = ecdsa.GetPublicKey();
        std::string id = ecdsa.GetAddress();
        auto member = std::make_shared<common::BftMember>(
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, 0, i == 0 ? 0 : -1);
        member->public_ip = common::IpToUint32("127.0.0.1");
        member->public_port = 123;
        members->push_back(member);
    }

    std::vector<transport::MessagePtr> verify_brd_msgs;
    auto latest_timeblock_info = std::make_shared<TimeBlockItem>();
    latest_timeblock_info->lastest_time_block_tm = common::TimeUtils::TimestampSeconds() - 10;
    latest_timeblock_info->latest_time_block_height = 1;
    latest_timeblock_info->vss_random = common::Random::RandomUint64();
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].OnNewElectionBlock(1, members, latest_timeblock_info);
        dkg[i].local_member_index_ = i;
        dkg[i].BroadcastVerfify(0);
        verify_brd_msgs.push_back(dkg[i].ver_brd_msg_);
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = verify_brd_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
    }

    // swap sec key
    std::vector<transport::MessagePtr> swap_sec_msgs;
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].SwapSecKey(0);
        swap_sec_msgs.push_back(dkg[i].sec_swap_msgs_);
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = swap_sec_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
//         dkg[i].all_secret_key_contribution_[i][3] = libff::alt_bn128_Fr::zero();
//         dkg[i].all_secret_key_contribution_[i][6] = libff::alt_bn128_Fr::zero();
//         dkg[i].all_secret_key_contribution_[i][9] = libff::alt_bn128_Fr::zero();
//         dkg[i].invalid_node_map_[3] = 9;
//         dkg[i].invalid_node_map_[6] = 9;
//         dkg[i].invalid_node_map_[9] = 9;
    }

    // sign and verify
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);
    std::vector<libff::alt_bn128_G1> all_signs;
    for (uint32_t i = 0; i < n; ++i) {
        dkg[i].FinishNoLock(0);
    }

    size_t count = 0;
    std::vector<size_t> idx_vec;
    for (size_t i = 0; i < n; ++i) {
        if (i == 3 || i == 6 || i == 9) {
            continue;
        }
     
        BlsSign bls_sign;
        libff::alt_bn128_G1 sign;
        bls_sign.Sign(t, n, dkg[i].local_sec_key_, hash, &sign);
        std::string verify_hash;
        ASSERT_EQ(
            bls_sign.Verify(t, n, sign, hash, dkg[i].local_publick_key_, &verify_hash),
            kBlsSuccess);
        all_signs.push_back(sign);


        idx_vec.push_back(i + 1);
        ++count;
        if (count >= t) {
            break;
        }
    }

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);

    for (uint32_t i = 0; i < n; ++i) {
        BlsSign bls_sign;
        std::string verify_hash;
        EXPECT_EQ(
            bls_sign.Verify(t, n, agg_sign, hash, dkg[i].common_public_key_, &verify_hash),
            kBlsSuccess);
    }
}

TEST_F(TestBls, ThreeRatioFail) {
    // t = 7, n = 10
    static const uint32_t t = 7;
    static const uint32_t n = 10;

    BlsDkg dkg[n];
    for (uint32_t i = 0; i < n; i++) {
        dkg[i].Init(bls_manager, security_ptr, 0, 0, libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(), libff::alt_bn128_G2::zero(), db_ptr);
    }
    common::MembersPtr members = std::make_shared<common::Members>();
    std::vector<std::string> pri_vec;
    for (uint32_t i = 0; i < n; ++i) {
        pri_vec.push_back(common::Random::RandomString(32));
    }

    for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        security::Ecdsa ecdsa;
        ecdsa.SetPrivateKey(pri_vec[i]);
        std::string pubkey_str = ecdsa.GetPublicKey();
        std::string id = ecdsa.GetAddress();
        auto member = std::make_shared<common::BftMember>(
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, 0, i == 0 ? 0 : -1);
        member->public_ip = common::IpToUint32("127.0.0.1");
        member->public_port = 123;
        members->push_back(member);
    }

    std::vector<transport::MessagePtr> verify_brd_msgs;
    auto latest_timeblock_info = std::make_shared<TimeBlockItem>();
    latest_timeblock_info->lastest_time_block_tm = common::TimeUtils::TimestampSeconds() - 10;
    latest_timeblock_info->latest_time_block_height = 1;
    latest_timeblock_info->vss_random = common::Random::RandomUint64();
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].OnNewElectionBlock(1, members, latest_timeblock_info);
        dkg[i].local_member_index_ = i;
        dkg[i].BroadcastVerfify(0);
        verify_brd_msgs.push_back(dkg[i].ver_brd_msg_);
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = verify_brd_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
    }

    // swap sec key
    std::vector<transport::MessagePtr> swap_sec_msgs;
    for (uint32_t i = 0; i < n; ++i) {
        auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        bls_manager->security_ = tmp_security_ptr;
        dkg[i].security_ = tmp_security_ptr;
        SetGloableInfo(pri_vec[i], network::kConsensusShardBeginNetworkId);
        dkg[i].SwapSecKey(0);
        swap_sec_msgs.push_back(dkg[i].sec_swap_msgs_);
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }

            auto tmp_security_ptr = std::make_shared<security::Ecdsa>();
            tmp_security_ptr->SetPrivateKey(pri_vec[j]);
            bls_manager->security_ = tmp_security_ptr;
            dkg[j].security_ = tmp_security_ptr;
            SetGloableInfo(pri_vec[j], network::kConsensusShardBeginNetworkId);
            auto msg_ptr = swap_sec_msgs[i];
            msg_ptr->thread_idx = 0;
            dkg[j].HandleMessage(msg_ptr);
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
//         dkg[i].all_secret_key_contribution_[i][3] = libff::alt_bn128_Fr::zero();
//         dkg[i].all_secret_key_contribution_[i][6] = libff::alt_bn128_Fr::zero();
//         dkg[i].all_secret_key_contribution_[i][7] = libff::alt_bn128_Fr::zero();
//         dkg[i].all_secret_key_contribution_[i][9] = libff::alt_bn128_Fr::zero();
        dkg[i].invalid_node_map_[3].insert(9);
        dkg[i].invalid_node_map_[6].insert(9);
        dkg[i].invalid_node_map_[7].insert(9);
        dkg[i].invalid_node_map_[9].insert(9);
    }

    // sign and verify
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);
    std::vector<libff::alt_bn128_G1> all_signs;
    for (uint32_t i = 0; i < n; ++i) {
        dkg[i].FinishNoLock(0);
        ASSERT_FALSE(dkg[i].finished_);
    }

    size_t count = 0;
    std::vector<size_t> idx_vec;
    for (size_t i = 0; i < n; ++i) {
        if (i == 3 || i == 6 || i == 7 || i == 9) {
            continue;
        }

        BlsSign bls_sign;
        libff::alt_bn128_G1 sign;
        bls_sign.Sign(t, n, dkg[i].local_sec_key_, hash, &sign);
        std::string verify_hash;
        ASSERT_EQ(
            bls_sign.Verify(t, n, sign, hash, dkg[i].local_publick_key_, &verify_hash),
            kBlsError);
        all_signs.push_back(sign);
        idx_vec.push_back(i + 1);
        ++count;
        if (count >= t) {
            break;
        }
    }

    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
        libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs);

        for (uint32_t i = 0; i < n; ++i) {
            BlsSign bls_sign;
            std::string verify_hash;
            ASSERT_EQ(
                bls_sign.Verify(t, n, agg_sign, hash, dkg[i].common_public_key_, &verify_hash),
                kBlsSuccess);
        }
        ASSERT_TRUE(false);
    } catch (...) {
        ASSERT_TRUE(true);
    }
}

}  // namespace test

}  // namespace bls

}  // namespace zjchain
