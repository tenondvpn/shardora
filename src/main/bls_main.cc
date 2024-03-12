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

static std::shared_ptr<security::Security> security_ptr = nullptr;
static BlsManager* bls_manager = nullptr;
static std::shared_ptr<db::Db> db_ptr = nullptr;
static const int32_t kThreadCount = 8;

class BlsVerify {
public:
    BlsVerify() {}

    ~BlsVerify() {}

    void Init() {
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

    void InitBlsVerificationValue() {
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

    void WriteDefaultLogConf(
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

    void SetGloableInfo(const std::string& private_key, uint32_t network_id) {
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

    void JoinNetwork(uint32_t network_id) {
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

    void LocalCreateContribution(std::shared_ptr<security::Security> sec_ptr) {
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

    void GetPrivateKey(std::vector<std::string>& pri_vec, uint32_t n) {
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
};

}  // namespace bls

}  // namespace zjchain

int main(int argc, char** argv) {
    using namespace zjchain;
    bls::BlsVerify verify;
    verify.Init();
    return 0;
}
