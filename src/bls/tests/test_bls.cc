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
#include "bls/polynomial.h"
#include "protos/prefix_db.h"
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
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("./db");
        bls_manager = new BlsManager(security_ptr, db_ptr);
        InitBlsVerificationValue();
    }

    static void InitBlsVerificationValue() {
        auto prefix_db = std::make_shared<protos::PrefixDb>(db_ptr);
        FILE* rlocal_bls_fd = fopen("../../src/bls/saved_verify_one", "r");
        if (rlocal_bls_fd != nullptr) {
            char* line = new char[1024 * 1024];
            uint32_t idx = 0;
            while (!feof(rlocal_bls_fd)) {
                fgets(line, 1024 * 1024, rlocal_bls_fd);
                std::string val = common::Encode::HexDecode(std::string(line, strlen(line) - 1));
                uint32_t* int_data = (uint32_t*)val.c_str();
                uint32_t idx = int_data[1];
                bls::protobuf::BlsVerifyValue verify_val;
                ASSERT_TRUE(verify_val.ParseFromArray(val.c_str() + 8, val.size() - 8));
                prefix_db->SavePresetVerifyValue(idx, 0, verify_val);
                ++idx;
                if (idx >= 1024) {
                    break;
                }
            }

            delete[] line;
            fclose(rlocal_bls_fd);
        }
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

    static void LocalCreateContribution(std::shared_ptr<security::Security> sec_ptr) {
        auto n = common::GlobalInfo::Instance()->each_shard_max_members();
        auto t = common::GetSignerCount(n);
        libBLS::Dkg dkg_instance(t, n);
        std::vector<libff::alt_bn128_Fr> polynomial = dkg_instance.GeneratePolynomial();
        bls::protobuf::LocalPolynomial local_poly;
        for (uint32_t i = 0; i < polynomial.size(); ++i) {
            local_poly.add_polynomial(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(polynomial[i])));
        }

        bls::protobuf::VerifyVecBrdReq bls_verify_req;
        auto g2_vec = dkg_instance.VerificationVector(polynomial);
        for (uint32_t i = 0; i < t; ++i) {
            bls::protobuf::VerifyVecItem& verify_item = *bls_verify_req.add_verify_vec();
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
        
        auto str = bls_verify_req.SerializeAsString();
        auto prefix_db = std::make_shared<protos::PrefixDb>(db_ptr);
        prefix_db->AddBlsVerifyG2(sec_ptr->GetAddress(), bls_verify_req);
        prefix_db->SaveLocalPolynomial(sec_ptr, sec_ptr->GetAddress(), local_poly);
    }
};

TEST_F(TestBls, TestPolynomial) {
//     Polynomial polynomial(db_ptr);
//     polynomial.GenesisInit(16, 1024);
}

TEST_F(TestBls, ContributionSignAndVerify) {
    SetGloableInfo(common::Random::RandomString(32), network::kConsensusShardBeginNetworkId);
    auto btime0 = common::TimeUtils::TimestampUs();

    std::vector<std::string> pri_vec;
    static const uint32_t n = 10;
    static const uint32_t t = common::GetSignerCount(n);
    static const uint32_t valid_t = t;
    static const uint32_t valid_count = n;

    BlsDkg* dkg = new BlsDkg[n];
    GetPrivateKey(pri_vec, n);
    ASSERT_EQ(pri_vec.size(), n);
    BlsDkg* dkg = new BlsDkg[n];
    system("sudo rm -rf ./db_*");
    for (uint32_t i = 0; i < n; i++) {
        std::shared_ptr<security::Security> tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        LocalCreateContribution(tmp_security_ptr);
        dkg[i].Init(
            bls_manager,
            tmp_security_ptr,
            t,
            n,
            libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(),
            libff::alt_bn128_G2::zero(),
            db_ptr);
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
        common_public_key = common_public_key + dkg[i].g2_vec_[0];
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
            if (pri_vec.size() == n) {
                break;
            }
        }

        fclose(prikey_fd);
    }

    ASSERT_TRUE(pri_vec.size() <= n);
    ASSERT_TRUE(pri_vec.size() <= 1024);
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
    static const int32_t kThreadCount = 1;
    std::vector<transport::MessagePtr> tmp_verify_brd_msgs[kThreadCount];
    auto test_func = [&](uint32_t b, uint32_t e, uint32_t thread_idx) {
        for (uint32_t i = b; i < e; ++i) {
            dkg[i].OnNewElectionBlock(1, members, latest_timeblock_info);
            dkg[i].local_member_index_ = i;
            dkg[i].BroadcastVerfify(0);
            tmp_verify_brd_msgs[thread_idx].push_back(dkg[i].ver_brd_msg_);
            ASSERT_EQ(dkg[i].ver_brd_msg_->header.bls_proto().elect_height(), 1);
            dkg[i].ver_brd_msg_ = nullptr;
        }
    };

    std::vector<std::thread> thread_vec;
    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        int32_t b = (pri_vec.size() / kThreadCount) * thread_idx;
        int32_t e = (pri_vec.size() / kThreadCount) * (thread_idx + 1);
        if (thread_idx == kThreadCount - 1) {
            e += pri_vec.size() % kThreadCount;
        }

        thread_vec.push_back(std::thread(test_func, b, e, thread_idx));
    }

    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        thread_vec[thread_idx].join();
    }

    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        for (uint32_t j = 0; j < tmp_verify_brd_msgs[thread_idx].size(); ++j) {
            verify_brd_msgs.push_back(tmp_verify_brd_msgs[thread_idx][j]);
        }
    }
}

static void GetSwapSeckeyMessage(
        BlsDkg* dkg,
        int32_t n,
        std::vector<transport::MessagePtr>& swap_sec_msgs) {
    static const int32_t kThreadCount = 4;
    std::vector<transport::MessagePtr> swap_sec_msgs_thread[kThreadCount];
    std::vector<transport::MessagePtr> tmp_verify_brd_msgs[kThreadCount];
    auto test_func = [&](uint32_t b, uint32_t e, uint32_t thread_idx) {
        for (uint32_t i = b; i < e; ++i) {
            bls_manager->security_ = dkg[i].security_;
            dkg[i].SwapSecKey(0);
            swap_sec_msgs_thread[thread_idx].push_back(dkg[i].sec_swap_msgs_);
        }
    };

    std::vector<std::thread> thread_vec;
    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        int32_t b = (n / kThreadCount) * thread_idx;
        int32_t e = (n / kThreadCount) * (thread_idx + 1);
        if (thread_idx == kThreadCount - 1) {
            e += n % kThreadCount;
        }

        thread_vec.push_back(std::thread(test_func, b, e, thread_idx));
    }

    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        thread_vec[thread_idx].join();
    }

    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        for (uint32_t j = 0; j < swap_sec_msgs_thread[thread_idx].size(); ++j) {
            swap_sec_msgs.push_back(swap_sec_msgs_thread[thread_idx][j]);
        }
    }
}

static void HandleVerifyBroadcast(
        BlsDkg* dkg,
        const std::vector<std::string>& pri_vec,
        const std::vector<transport::MessagePtr>& verify_brd_msgs) {
    static const int32_t kThreadCount = 4;
    uint32_t n = pri_vec.size();
    std::vector<transport::MessagePtr> tmp_verify_brd_msgs[kThreadCount];
    auto test_func = [&](uint32_t b, uint32_t e, uint32_t thread_idx) {
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t j = b; j < e; ++j) {
                if (i == j) {
                    continue;
                }
                auto msg_ptr = verify_brd_msgs[i];
                msg_ptr->thread_idx = 0;
                dkg[j].HandleMessage(msg_ptr);
            }
        }
    };

    std::vector<std::thread> thread_vec;
    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        int32_t b = (pri_vec.size() / kThreadCount) * thread_idx;
        int32_t e = (pri_vec.size() / kThreadCount) * (thread_idx + 1);
        if (thread_idx == kThreadCount - 1) {
            e += pri_vec.size() % kThreadCount;
        }

        thread_vec.push_back(std::thread(test_func, b, e, thread_idx));
    }

    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        thread_vec[thread_idx].join();
    }
}

static void HandleSwapSeckey(
        BlsDkg* dkg,
        const std::vector<std::string>& pri_vec,
        const std::vector<transport::MessagePtr>& swap_seckey_msgs) {
    static const int32_t kThreadCount = 4;
    uint32_t n = pri_vec.size();
    std::vector<transport::MessagePtr> tmp_verify_brd_msgs[kThreadCount];
    auto test_func = [&](uint32_t b, uint32_t e, uint32_t thread_idx) {
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t j = b; j < e; ++j) {
                if (i == j) {
                    continue;
                }
                auto msg_ptr = swap_seckey_msgs[i];
                msg_ptr->thread_idx = 0;
                dkg[j].HandleMessage(msg_ptr);
            }
        }
    };

    std::vector<std::thread> thread_vec;
    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        int32_t b = (pri_vec.size() / kThreadCount) * thread_idx;
        int32_t e = (pri_vec.size() / kThreadCount) * (thread_idx + 1);
        if (thread_idx == kThreadCount - 1) {
            e += pri_vec.size() % kThreadCount;
        }

        thread_vec.push_back(std::thread(test_func, b, e, thread_idx));
    }

    for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
        thread_vec[thread_idx].join();
    }
}

TEST_F(TestBls, LagrangeCoeffs) {
    const uint32_t n = 1024;
    const uint32_t t = common::GetSignerCount(n);
    std::vector<libff::alt_bn128_G1> signs;
    std::vector<size_t> idx_vec_all(n);
    for (int32_t i = 0; i < n; ++i) {
        idx_vec_all[i] = i + 1;
        if (i < t) {
            signs.push_back(libff::alt_bn128_Fr::random_element() * libff::alt_bn128_G1::one());
        }
    }

    std::random_shuffle(idx_vec_all.begin(), idx_vec_all.end());
    std::vector<size_t> idx_vec(idx_vec_all.begin(), idx_vec_all.begin() + t);
    std::sort(idx_vec.begin(), idx_vec.end());
    ASSERT_TRUE(idx_vec[1] > idx_vec[0]);
    auto time5 = common::TimeUtils::TimestampUs();
    std::vector< libff::alt_bn128_Fr > lagrange_coeffs(t);
    libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);
    auto time61 = common::TimeUtils::TimestampUs();
    std::unordered_set<std::string> unique_set;
    for (int32_t i = 0; i < lagrange_coeffs.size(); ++i) {
        unique_set.insert(libBLS::ThresholdUtils::fieldElementToString(lagrange_coeffs[i]));
    }
    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    std::cout << "LagrangeCoeffs : " << (time61 - time5)
        << ", size: " << unique_set.size()
        << ", real size: " << lagrange_coeffs.size() << std::endl;

    time5 = common::TimeUtils::TimestampUs();
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        signs,
        lagrange_coeffs);
    auto time6 = common::TimeUtils::TimestampUs();
    std::cout << "SignatureRecover 5: " << (time6 - time5) << std::endl;
}

TEST_F(TestBls, FileSigns) {
    static const uint32_t n = 1024;
    static const uint32_t t = common::GetSignerCount(n);
    FILE* fd_signs = fopen("signs", "r");
    char* data = new char[1024 * 1024 * 10];
    size_t len = fread(data, 1, 10 * 1024 * 1024, fd_signs);
    fclose(fd_signs);
    bls::protobuf::VerifyVecBrdReq proto_signs;
    ASSERT_TRUE(proto_signs.ParseFromArray(data, len));
    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec(t);
    for (int32_t i = 0; i < proto_signs.verify_vec_size() - 1; ++i) {
        auto X = libff::alt_bn128_Fq(common::Encode::HexEncode(proto_signs.verify_vec(i).x_c0()).c_str());
        auto Y = libff::alt_bn128_Fq(common::Encode::HexEncode(proto_signs.verify_vec(i).x_c1()).c_str());
        auto Z = libff::alt_bn128_Fq(common::Encode::HexEncode(proto_signs.verify_vec(i).y_c0()).c_str());
        all_signs.push_back(libff::alt_bn128_G1(X, Y, Z));
        idx_vec[i] = i + 1;
        if (all_signs.size() >= t) {
            break;
        }
    }

    auto& item = proto_signs.verify_vec(proto_signs.verify_vec_size() - 1);
    auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
    auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
    auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
    auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
    auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
    auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
    auto common_pk = libff::alt_bn128_G2(x_coord, y_coord, z_coord);

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    auto time5 = common::TimeUtils::TimestampUs();
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
    auto time61 = common::TimeUtils::TimestampUs();
    std::cout << "LagrangeCoeffs : " << (time61 - time5) << std::endl;
    time5 = time61;
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);
    auto time6 = common::TimeUtils::TimestampUs();
    std::cout << "SignatureRecover 5: " << (time6 - time5) << std::endl;
    std::string verify_hash;
    auto hash_str = common::Hash::Sha256("hello world");
    libff::alt_bn128_G1 hash;
    BlsSign bls_sign;
    ASSERT_EQ(bls_sign.GetLibffHash(hash_str, &hash), kBlsSuccess);

    // slow
    ASSERT_EQ(
        bls_sign.Verify(t, n, agg_sign, hash, common_pk, &verify_hash),
        kBlsSuccess);
    auto time7 = common::TimeUtils::TimestampUs();
    std::cout << "2: " << (time7 - time6) << std::endl;
}

TEST_F(TestBls, AllSuccess) {
//     static const uint32_t t = 700;
//     static const uint32_t n = 1024;
    std::vector<std::string> pri_vec;
    static const uint32_t n = 10;
    static const uint32_t t = common::GetSignerCount(n);

    GetPrivateKey(pri_vec, n);
    ASSERT_EQ(pri_vec.size(), n);
    BlsDkg* dkg = new BlsDkg[n];
    system("sudo rm -rf ./db_*");
    for (uint32_t i = 0; i < n; i++) {
        std::shared_ptr<security::Security> tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        dkg[i].Init(
            bls_manager,
            tmp_security_ptr,
            t,
            n,
            libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(),
            libff::alt_bn128_G2::zero(),
            db_ptr);
    }

    static const uint32_t valid_n = pri_vec.size();
    static const uint32_t valid_t = common::GetSignerCount(valid_n);
    libBLS::Dkg dkg_instance = libBLS::Dkg(valid_t, valid_n);
    std::vector<std::vector<libff::alt_bn128_Fr>> polynomial(valid_n);
    for (auto& pol : polynomial) {
        pol = dkg_instance.GeneratePolynomial();
    }

    auto prefix_db = std::make_shared<protos::PrefixDb>(db_ptr);
    common::MembersPtr members = std::make_shared<common::Members>();
    for (uint32_t idx = 0; idx < pri_vec.size(); ++idx) {
        auto tmp_security_ptr = dkg[idx].security_;
        std::string pubkey_str = tmp_security_ptr->GetPublicKey();
        std::string id = tmp_security_ptr->GetAddress();
        auto member = std::make_shared<common::BftMember>(
            network::kConsensusShardBeginNetworkId, id, pubkey_str, idx, idx == 0 ? 0 : -1);
        member->public_ip = common::IpToUint32("127.0.0.1");
        member->public_port = 123;
        members->push_back(member);

        bls::protobuf::LocalPolynomial local_poly;
        for (uint32_t j = 0; j < polynomial[idx].size(); ++j) {
            local_poly.add_polynomial(common::Encode::HexDecode(
                libBLS::ThresholdUtils::fieldElementToString(polynomial[idx][j])));
        }

        bls::protobuf::JoinElectInfo join_info;
        join_info.set_member_idx(idx);
        uint32_t sharding_id = 3;
        join_info.set_shard_id(sharding_id);
        auto* req = join_info.mutable_g2_req();
        auto g2_vec = dkg_instance.VerificationVector(polynomial[idx]);
        auto contributions = dkg_instance.SecretKeyContribution(polynomial[idx]);
        auto contributions1 = dkg_instance.SecretKeyContribution(polynomial[idx]);
        for (uint32_t i = 0; i < contributions.size(); ++i) {
            ASSERT_TRUE(dkg_instance.Verification(i, contributions[i], g2_vec));
            ASSERT_TRUE(dkg_instance.Verification(i, contributions1[i], g2_vec));
            ASSERT_TRUE(contributions[i] == contributions1[i]);
        }

        std::vector<libff::alt_bn128_G2> verify_g2_vec;
        std::string str_for_hash;
        str_for_hash.append((char*)&sharding_id, sizeof(sharding_id));
        str_for_hash.append((char*)&idx, sizeof(idx));
        for (uint32_t i = 0; i < valid_t; ++i) {
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

            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(verify_item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(verify_item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(verify_item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(verify_item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(verify_item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(verify_item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            auto g2 = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
            verify_g2_vec.push_back(g2);

            str_for_hash.append(verify_item.x_c0());
            str_for_hash.append(verify_item.x_c1());
            str_for_hash.append(verify_item.y_c0());
            str_for_hash.append(verify_item.y_c1());
            str_for_hash.append(verify_item.z_c0());
            str_for_hash.append(verify_item.z_c1());
        }

        auto check_hash = common::Hash::keccak256(str_for_hash);
        for (uint32_t tmp_idx = 0; tmp_idx < pri_vec.size(); ++tmp_idx) {
            if (tmp_idx == idx) {
                continue;
            }

            auto all_pos_count = pri_vec.size() / common::kElectNodeMinMemberIndex + 1;
            std::vector<libff::alt_bn128_G2> verify_g2s(all_pos_count, libff::alt_bn128_G2::zero());
            for (int32_t vidx = 0; vidx < verify_g2_vec.size(); ++vidx) {
                for (int32_t j = 0; j < all_pos_count; ++j) {
                    auto midx = tmp_idx + j * common::kElectNodeMinMemberIndex;
                    verify_g2s[j] = verify_g2s[j] + power(libff::alt_bn128_Fr(midx + 1), vidx) * verify_g2_vec[vidx];
                }
            }

            // bls::protobuf::JoinElectBlsInfo verfy_final_vals;
            // for (uint32_t i = 0; i < verify_g2s.size(); ++i) {
            //     auto midx = tmp_idx + i * common::kElectNodeMinMemberIndex;
            //     ASSERT_TRUE(verify_g2s[i] == contributions[midx] * libff::alt_bn128_G2::one());
                
            //     bls::protobuf::VerifyVecItem& verify_item = *verfy_final_vals.mutable_verify_req()->add_verify_vec();
            //     verify_item.set_x_c0(common::Encode::HexDecode(
            //         libBLS::ThresholdUtils::fieldElementToString(verify_g2s[i].X.c0)));
            //     verify_item.set_x_c1(common::Encode::HexDecode(
            //         libBLS::ThresholdUtils::fieldElementToString(verify_g2s[i].X.c1)));
            //     verify_item.set_y_c0(common::Encode::HexDecode(
            //         libBLS::ThresholdUtils::fieldElementToString(verify_g2s[i].Y.c0)));
            //     verify_item.set_y_c1(common::Encode::HexDecode(
            //         libBLS::ThresholdUtils::fieldElementToString(verify_g2s[i].Y.c1)));
            //     verify_item.set_z_c0(common::Encode::HexDecode(
            //         libBLS::ThresholdUtils::fieldElementToString(verify_g2s[i].Z.c0)));
            //     verify_item.set_z_c1(common::Encode::HexDecode(
            //         libBLS::ThresholdUtils::fieldElementToString(verify_g2s[i].Z.c1)));
            // }

            // verfy_final_vals.set_src_hash(check_hash);
            // auto verified_val = verfy_final_vals.SerializeAsString();
            // prefix_db->SaveVerifiedG2s(tmp_idx, id, verify_g2s.size(), verfy_final_vals);
// 
//             auto old_g2 = polynomial[tmp_idx][0] * libff::alt_bn128_G2::one();
//             polynomial[tmp_idx][0] = libff::alt_bn128_Fr::random_element();
//             g2_vec = dkg_instance.VerificationVector(polynomial[tmp_idx]);
//             contributions = dkg_instance.SecretKeyContribution(polynomial[tmp_idx]);
//             for (uint32_t i = 0; i < contributions.size(); ++i) {
//                 ASSERT_TRUE(dkg_instance.Verification(i, contributions[i], g2_vec));
//             }
// 
//             auto new_g2 = polynomial[tmp_idx][0] * libff::alt_bn128_G2::one();
//             auto old1 = power(libff::alt_bn128_Fr(tmp_idx + 1), 0) * old_g2;
//             auto new1 = power(libff::alt_bn128_Fr(tmp_idx + 1), 0) * new_g2;
//             verify_g2s[0] = verify_g2s[0] - old1 + new1;
//             ASSERT_TRUE(verify_g2s[0] == contributions[tmp_idx] * libff::alt_bn128_G2::one());
//             std::cout << "change tmp_idx verify success." << std::endl;
        }

        auto str = join_info.SerializeAsString();
        prefix_db->SaveTemporaryKv(check_hash, str);
        prefix_db->AddBlsVerifyG2(tmp_security_ptr->GetAddress(), *req);
        prefix_db->SaveLocalPolynomial(tmp_security_ptr, tmp_security_ptr->GetAddress(), local_poly);
    }

    auto time0 = common::TimeUtils::TimestampUs();
    std::vector<transport::MessagePtr> verify_brd_msgs;
    auto latest_timeblock_info = std::make_shared<TimeBlockItem>();
    latest_timeblock_info->lastest_time_block_tm = common::TimeUtils::TimestampSeconds() - 10;
    latest_timeblock_info->latest_time_block_height = 1;
    latest_timeblock_info->vss_random = common::Random::RandomUint64();
    std::cout << "now create contribution." << std::endl;
    CreateContribution(members, dkg, pri_vec, latest_timeblock_info, verify_brd_msgs);
    auto time1 = common::TimeUtils::TimestampUs();
    std::cout << "0: " << (time1 - time0) << std::endl;
    HandleVerifyBroadcast(dkg, pri_vec, verify_brd_msgs);
    auto tmp_vec = std::vector<transport::MessagePtr>();
    verify_brd_msgs.swap(tmp_vec);
    auto time2 = common::TimeUtils::TimestampUs();
    std::cout << "1: " << (time2 - time1) << std::endl;
    // swap sec key
    std::vector<transport::MessagePtr> swap_sec_msgs;
    GetSwapSeckeyMessage(dkg, n, swap_sec_msgs);
    ASSERT_EQ(swap_sec_msgs.size(), n);
    auto time3 = common::TimeUtils::TimestampUs();
    std::cout << "2: " << (time3 - time2) << std::endl;
    HandleSwapSeckey(dkg, pri_vec, swap_sec_msgs);
    swap_sec_msgs.swap(tmp_vec);
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
        dkg[i].FinishBroadcast(0);
        ASSERT_TRUE(dkg[i].finished_);
    }

    bls::protobuf::VerifyVecBrdReq proto_signs;
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
        bls::protobuf::VerifyVecItem& verify_item = *proto_signs.add_verify_vec();
        verify_item.set_x_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(sign.X)));
        verify_item.set_x_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(sign.Y)));
        verify_item.set_y_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(sign.Z)));
        if (all_signs.size() < t) {
            all_signs.push_back(sign);
            idx_vec[i] = i + 1;
        }
    }

    bls::protobuf::VerifyVecItem& verify_item = *proto_signs.add_verify_vec();
    verify_item.set_x_c0(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(dkg[0].common_public_key_.X.c0)));
    verify_item.set_x_c1(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(dkg[0].common_public_key_.X.c1)));
    verify_item.set_y_c0(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(dkg[0].common_public_key_.Y.c0)));
    verify_item.set_y_c1(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(dkg[0].common_public_key_.Y.c1)));
    verify_item.set_z_c0(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(dkg[0].common_public_key_.Z.c0)));
    verify_item.set_z_c1(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(dkg[0].common_public_key_.Z.c1)));

    FILE* fd_signs = fopen("signs", "w");
    std::string signs_val = common::Encode::HexEncode(proto_signs.SerializeAsString());
    fwrite(signs_val.c_str(), 1, signs_val.size(), fd_signs);
    fclose(fd_signs);

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    auto time5 = common::TimeUtils::TimestampUs();
    std::cout << "4: " << (time5 - time4) << std::endl;
    auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
    auto time61 = common::TimeUtils::TimestampUs();
    std::cout << "LagrangeCoeffs : " << (time61 - time5) << std::endl;
    time5 = time61;
    libff::alt_bn128_G1 agg_sign = bls_instance.SignatureRecover(
        all_signs,
        lagrange_coeffs);
    auto time6 = common::TimeUtils::TimestampUs();
    std::cout << "SignatureRecover 5: " << (time6 - time5) << std::endl;
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
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, i == 0 ? 0 : -1);
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
        dkg[i].FinishBroadcast(0);
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
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, i == 0 ? 0 : -1);
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
        dkg[i].FinishBroadcast(0);
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
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, i == 0 ? 0 : -1);
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
        dkg[i].FinishBroadcast(0);
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
            network::kConsensusShardBeginNetworkId, id, pubkey_str, i, i == 0 ? 0 : -1);
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
        dkg[i].FinishBroadcast(0);
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
