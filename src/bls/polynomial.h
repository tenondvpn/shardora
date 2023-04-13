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
    Polynomial() {}

    ~Polynomial() {}

    int Init(std::shared_ptr<db::Db>& db, uint16_t t, uint16_t n) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
        bls::protobuf::LocalBlsItem saved_polynomial;
        if (!prefix_db_->GetPresetPolynomial(&saved_polynomial)) {
            return kBlsError;
        }

        if (saved_polynomial.polynomial_size() != saved_polynomial.verify_vec_size()) {
            return kBlsError;
        }

        for (int32_t i = 0; i < saved_polynomial.polynomial_size(); ++i) {
            polynomial_.push_back(libff::alt_bn128_Fr(saved_polynomial.polynomial(i).c_str()));
            auto& item = saved_polynomial.verify_vec(i);
            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            g2_vec_.push_back(libff::alt_bn128_G2(x_coord, y_coord, z_coord));
            bls::protobuf::BlsVerifyValue verify_val;
            for (uint16_t j = 0; j < n; ++j) {
                if (!prefix_db_->GetPresetVerifyValue(j, i, &verify_val)) {
                    return kBlsError;
                }

                if (verify_val.verify_vec_size() != t) {
                    return kBlsError;
                }

                for (uint16_t k = 0; k < verify_val.verify_vec_size(); ++k) {
                    auto& item = verify_val.verify_vec(k);
                    auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
                    auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
                    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
                    auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
                    auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
                    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
                    auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
                    auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
                    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
                    uint64_t key = (((uint64_t)j) << 48) | (((uint64_t)k) << 32) | ((uint64_t)i);
                    verify_values_[key] = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
                }
            }
        }
        
        return kBlsSuccess;
    }

    int GenesisInit(std::shared_ptr<db::Db>& db, uint16_t count, uint16_t n) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
        auto dkg_instance = std::make_shared<libBLS::Dkg>(count, count);
        auto polynomial = dkg_instance->GeneratePolynomial();
        std::vector<libff::alt_bn128_G2> g2_vec(count);
        for (size_t i = 0; i < count; ++i) {
            g2_vec[i] = polynomial[i] * libff::alt_bn128_G2::one();
        }

        bls::protobuf::LocalBlsItem saved_polynomial;
        for (int32_t i = 0; i < g2_vec.size(); ++i) {
            bls::protobuf::VerifyVecItem& verify_item = *saved_polynomial.add_verify_vec();
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

        for (uint32_t i = 0; i < polynomial.size(); ++i) {
            saved_polynomial.add_polynomial(
                libBLS::ThresholdUtils::fieldElementToString(polynomial[i]));
        }

        FILE* saved_polynomial_fd = fopen("saved_polynomial", "w");
        prefix_db_->SavePresetPolynomial(saved_polynomial);
        std::string val = common::Encode::HexEncode(saved_polynomial.SerializeAsString());
        fwrite(val.c_str(), 1, val.size(), saved_polynomial_fd);
        fclose(saved_polynomial_fd);

        uint16_t t = common::GetSignerCount(n);
        static const uint32_t kThreadCount = 8;
        auto btime = common::TimeUtils::TimestampUs();
        auto run_func = [&](int32_t b, int32_t e, int thread_idx) {
            std::string file = std::string("saved_verify_one_") + std::to_string(thread_idx);
            FILE* saved_verify_fd = fopen(file.c_str(), "a+");
            for (int32_t idx = b; idx < e; ++idx) {
                bls::protobuf::BlsVerifyValue verify_val;
                for (size_t tidx = 0; tidx < t; ++tidx) {
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
    std::vector<libff::alt_bn128_Fr> polynomial_;
    std::vector<libff::alt_bn128_G2> g2_vec_;
    std::unordered_map<uint64_t, libff::alt_bn128_G2> verify_values_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
};

}  // namespace bls

}  // namespace zjchain
