#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <gtest/gtest.h>

#define ZJC_UNITTEST
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

static std::shared_ptr<security::Security> security_ptr = nullptr;
static BlsManager* bls_manager = nullptr;
static std::shared_ptr<db::Db> db_ptr = nullptr;
static const int32_t kThreadCount = 8;

class BlsVerify {
public:
    BlsVerify() {}

    ~BlsVerify() {}

    static void Init() {
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
        ZJC_DEBUG("SaveLocalPolynomial success: %s",
            common::Encode::HexEncode(sec_ptr->GetAddress()).c_str());
    }

    static void GetPrivateKey(std::vector<std::string>& pri_vec, uint32_t n) {
        auto file_name = (std::string("prikey_") + std::to_string(n)).c_str();
        FILE* prikey_fd = fopen(file_name, "r");
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

        if (pri_vec.empty()) {
            FILE* prikey_fd = fopen(file_name, "w");
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
                if (dkg[i].ver_brd_msg_->header.bls_proto().elect_height() != 1) {
                    std::cout << "elect height failed!" << std::endl;
                    exit(0);
                }

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
                    dkg[j].HandleBlsMessage(msg_ptr);
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
                    dkg[j].HandleBlsMessage(msg_ptr);
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

    void CheckAllNodesVerify(uint32_t n, const std::vector<std::string>& pri_vec, bls::BlsDkg* dkg, bool exchange_single) {
        uint32_t t = common::GetSignerCount(pri_vec.size());
        auto t2 = common::TimeUtils::TimestampMs();
        uint32_t valid_n = pri_vec.size();
        uint32_t valid_t = common::GetSignerCount(valid_n);
        libBLS::Dkg dkg_instance = libBLS::Dkg(valid_t, valid_n);
        std::vector<std::vector<libff::alt_bn128_Fr>> polynomial(valid_n);
        std::cout << "now GeneratePolynomial all dkg" << std::endl;
        for (auto& pol : polynomial) {
            pol = dkg_instance.GeneratePolynomial();
        }

        auto t3 = common::TimeUtils::TimestampMs();
        std::cout << "GeneratePolynomial success use time: " << (t3 - t2) << std::endl;
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
        }

        db::DbWriteBatch db_batchs[pri_vec.size()];
        auto t4 = common::TimeUtils::TimestampMs();
        auto callback = [&](uint32_t idx) {
            auto btime = common::TimeUtils::TimestampMs();
            auto& db_batch = db_batchs[idx];
            std::string id = dkg[idx].security_->GetAddress();
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
            // auto contributions = dkg_instance.SecretKeyContribution(polynomial[idx]);
            // auto contributions1 = dkg_instance.SecretKeyContribution(polynomial[idx]);
            // for (uint32_t i = 0; i < contributions.size(); ++i) {
            //     ASSERT_TRUE(dkg_instance.Verification(i, contributions[i], g2_vec));
            //     ASSERT_TRUE(dkg_instance.Verification(i, contributions1[i], g2_vec));
            //     ASSERT_TRUE(contributions[i] == contributions1[i]);
            // }

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

                libff::alt_bn128_G2 verify_g2 = libff::alt_bn128_G2::zero();
                for (int32_t vidx = 0; vidx < verify_g2_vec.size(); ++vidx) {
                    verify_g2 = verify_g2 + power(libff::alt_bn128_Fr(tmp_idx + 1), vidx) * verify_g2_vec[vidx];
                }

                if (exchange_single) {
                    auto btime = common::TimeUtils::TimestampMs();
                    auto old_val = verify_g2_vec[8];
                    auto new_val = verify_g2_vec[9];
                    for (uint32_t change_idx = 0; change_idx < (pri_vec.size() / 10); ++change_idx) {
                        auto old_g2_val = power(libff::alt_bn128_Fr(tmp_idx + 1), change_idx) * old_val;
                        auto new_g2_val = power(libff::alt_bn128_Fr(tmp_idx + 1), change_idx) * new_val;
                        verify_g2 = verify_g2 - old_g2_val + new_g2_val;
                    }

                    auto etime = common::TimeUtils::TimestampMs();
                    std::cout << "single all over use time: " << (etime - btime) << std::endl;
                    break;
                }
               
                bls::protobuf::JoinElectBlsInfo verfy_final_vals;
                bls::protobuf::VerifyVecItem& verify_item = *verfy_final_vals.mutable_verified_g2();
                verify_item.set_x_c0(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(verify_g2.X.c0)));
                verify_item.set_x_c1(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(verify_g2.X.c1)));
                verify_item.set_y_c0(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(verify_g2.Y.c0)));
                verify_item.set_y_c1(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(verify_g2.Y.c1)));
                verify_item.set_z_c0(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(verify_g2.Z.c0)));
                verify_item.set_z_c1(common::Encode::HexDecode(
                    libBLS::ThresholdUtils::fieldElementToString(verify_g2.Z.c1)));
                auto verified_val = verfy_final_vals.SerializeAsString();
                prefix_db->SaveVerifiedG2s(tmp_idx, id, valid_t, verfy_final_vals, db_batch);
            }

            
            auto str = join_info.SerializeAsString();
            prefix_db->SaveNodeVerificationVector(dkg[idx].security_->GetAddress(), join_info, db_batch);
            prefix_db->SaveTemporaryKv(check_hash, str, db_batch);
            prefix_db->AddBlsVerifyG2(dkg[idx].security_->GetAddress(), *req, db_batch);
            prefix_db->SaveLocalPolynomial(
                dkg[idx].security_, 
                dkg[idx].security_->GetAddress(), 
                local_poly, 
                false, 
                db_batch);
            auto etime = common::TimeUtils::TimestampMs();
            std::cout << "over " << idx << " use time: " << (etime - btime) << std::endl;
        };

        auto btime = common::TimeUtils::TimestampMs();
        // std::vector<std::shared_ptr<std::thread>> thread_vec;
        // for (uint32_t idx = 0; idx < pri_vec.size(); ++idx) {
        //     thread_vec.push_back(std::make_shared<std::thread>(callback, idx));
        //     if (thread_vec.size() >= kThreadCount || idx == (pri_vec.size() - 1)) {
        //         for (uint32_t i = 0; i < thread_vec.size(); ++i) {
        //             thread_vec[i]->join();
        //         }

        //         thread_vec.clear();
        //     }
        // }

        // for (uint32_t i = 0; i < pri_vec.size(); ++i) {
        //     db_ptr->Put(db_batchs[i]);
        // }

        callback(0);
        auto etime = common::TimeUtils::TimestampMs();
        std::cout << "all over use time: " << (etime - btime) << std::endl;

        return;
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
        std::vector<libff::alt_bn128_G1> all_signs;
        for (uint32_t i = 0; i < n; ++i) {
            dkg[i].FinishBroadcast(0);
            if (!dkg[i].finished_) {
                std::cout << "not finished " << i << std::endl;
                assert(false);
                exit(0);
            }
        }

        bls::protobuf::VerifyVecBrdReq proto_signs;
        std::vector<size_t> idx_vec(t);
        for (size_t i = 0; i < t; ++i) {
            BlsSign bls_sign;
            libff::alt_bn128_G1 sign;
            bls_sign.Sign(t, n, dkg[i].local_sec_key_, hash, &sign);
            std::string verify_hash;
            // slow
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
            if (bls_sign.Verify(t, n, agg_sign, hash, dkg[i].common_public_key_, &verify_hash) != kBlsSuccess) {
                std::cout << "Verify signature failed!" << std::endl;
                exit(0);
            }
        }

        auto time7 = common::TimeUtils::TimestampUs();
        std::cout << "6: " << (time7 - time6) << std::endl;
    }
};

}  // namespace bls

}  // namespace zjchain

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "invalid args: ./blsmain node_count";
        return 1;
    }

    uint32_t kN = 0;
    using namespace zjchain;
    common::StringUtil::ToUint32(argv[1], &kN);
    bls::BlsVerify::Init();

    system("sudo rm -rf ./db_* prikey*");
    uint32_t t = common::GetSignerCount(kN);
    std::vector<std::string> pri_vec;
    bls::BlsVerify::GetPrivateKey(pri_vec, kN);
    bls::BlsDkg* dkg = new bls::BlsDkg[kN];
    auto t1 = common::TimeUtils::TimestampMs();
    for (uint32_t i = 0; i < kN; i++) {
        std::shared_ptr<security::Security> tmp_security_ptr = std::make_shared<security::Ecdsa>();
        tmp_security_ptr->SetPrivateKey(pri_vec[i]);
        dkg[i].Init(
            bls::bls_manager,
            tmp_security_ptr,
            t,
            kN,
            libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(),
            libff::alt_bn128_G2::zero(),
            bls::db_ptr);
    }

    bls::BlsVerify verify;
    verify.CheckAllNodesVerify(kN, pri_vec, dkg, true);
    delete []dkg;
    // delete []exchange_dkg;
    return 0;
}
