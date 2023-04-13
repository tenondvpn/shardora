#pragma once

#include <memory>
#include <unordered_map>

#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>
#include <dkg/dkg.h>

#include "common/bitmap.h"
#include "common/log.h"
#include "common/limit_heap.h"
#include "common/node_members.h"
#include "common/utils.h"
#include "protos/prefix_db.h"

namespace zjchain {

namespace bls {

class Polynomial {
public:
    Polynomial(std::shared_ptr<db::Db>& db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    ~Polynomial() {}

    int GenesisInit(uint16_t count, uint16_t n) {
        uint16_t t = common::GetSignerCount(n);
        static const uint32_t kThreadCount = 8;
        auto btime = common::TimeUtils::TimestampUs();
        auto run_func = [&](int32_t b, int32_t e, int thread_idx) {
            std::string file = std::string("saved_verify_one_") + std::to_string(thread_idx);
            FILE* saved_verify_fd = fopen(file.c_str(), "a+");
            for (int32_t idx = b; idx < e; ++idx) {
                bls::protobuf::BlsVerifyValue verify_val;
                for (size_t tidx = 1; tidx < t; ++tidx) {
                    auto value = power(libff::alt_bn128_Fr(idx + 1), tidx) * libff::alt_bn128_G2::one();
                    bls::protobuf::VerifyVecItem& verify_item = *verify_val.add_verify_vec();
                    verify_item.set_x_c0(common::Encode::HexDecode(
                        libBLS::ThresholdUtils::fieldElementToString(value.X.c0)));
                    verify_item.set_x_c1(common::Encode::HexDecode(
                        libBLS::ThresholdUtils::fieldElementToString(value.X.c1)));
                    verify_item.set_y_c0(common::Encode::HexDecode(
                        libBLS::ThresholdUtils::fieldElementToString(value.Y.c0)));
                    verify_item.set_y_c1(common::Encode::HexDecode(
                        libBLS::ThresholdUtils::fieldElementToString(value.Y.c1)));
                    verify_item.set_z_c0(common::Encode::HexDecode(
                        libBLS::ThresholdUtils::fieldElementToString(value.Z.c0)));
                    verify_item.set_z_c1(common::Encode::HexDecode(
                        libBLS::ThresholdUtils::fieldElementToString(value.Z.c1)));
                }

                char data[8];
                uint32_t* int_data = (uint32_t*)data;
                int_data[0] = 0;
                int_data[1] = idx;
                std::string val = common::Encode::HexEncode(std::string(data, sizeof(data)) + verify_val.SerializeAsString()) + "\n";
                fwrite(val.c_str(), 1, val.size(), saved_verify_fd);
                prefix_db_->SavePresetVerifyValue(idx, 0, verify_val);
            }

            fclose(saved_verify_fd);
        };

        std::vector<std::thread> thread_vec;
        for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
            int32_t b = (n / kThreadCount) * thread_idx;
            int32_t e = (n / kThreadCount) * (thread_idx + 1);
            if (thread_idx == kThreadCount - 1) {
                e += n % kThreadCount;
            }

            std::cout << thread_idx << " : " << b << ", " << e << std::endl;
            thread_vec.push_back(std::thread(run_func, b, e, thread_idx));
        }

        for (int32_t thread_idx = 0; thread_idx < kThreadCount; ++thread_idx) {
            thread_vec[thread_idx].join();
        }

        auto etime = common::TimeUtils::TimestampUs();
        std::cout << "finished: " << 0 << ", use time us: " << (etime - btime) << std::endl;

        return kBlsSuccess;
    }

private:
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
};

}  // namespace bls

}  // namespace zjchain
