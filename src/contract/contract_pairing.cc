#include "contract/contract_pairing.h"

#include "common/time_utils.h"

namespace zjchain {

namespace contract {

PbcParing::PbcParing(const std::string& create_address, const std::string& pairing_param)
        : ContractInterface(create_address), pairing_(pairing_param) {}

PbcParing::~PbcParing() {}

int PbcParing::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    auto btime = common::TimeUtils::TimestampMs();
    SetupParams();
    KeyGen();
    GenTk();
    Encrypt();
    Transform();
    Decrypt();
    auto etime = common::TimeUtils::TimestampMs();
    CONTRACT_DEBUG("call use time: %d ms", etime - btime);
    return kContractSuccess;
}

void PbcParing::SaveData() {
    auto btime = common::TimeUtils::TimestampMs();
    SetupParams();
    KeyGen();
    GenTk();
    Encrypt();
//     Transform();
//     Decrypt();
    FILE* fd = fopen("./all_params", "w");
    fputs((std::string("o:") + common::Encode::HexEncode(o_.toString()) + "\n").c_str(), fd);
    fputs((std::string("c:") + common::Encode::HexEncode(C_.toString()) + "\n").c_str(), fd);
    fputs((std::string("c1:") + common::Encode::HexEncode(C1_.toString(true)) + "\n").c_str(), fd);
    fputs((std::string("l1:") + common::Encode::HexEncode(L1_.toString(true)) + "\n").c_str(), fd);
    fputs((std::string("r1:") + common::Encode::HexEncode(R1_.toString(true)) + "\n").c_str(), fd);
    fputs((std::string("n:") + std::to_string(count_) + "\n").c_str(), fd);
    for (int32_t i = 0; i < count_; i++) {
        fputs((std::string("c_") + std::to_string(i) + ":" + common::Encode::HexEncode(clist_[i].toString(true)) + "\n").c_str(), fd);
        fputs((std::string("d_") + std::to_string(i) + ":" + common::Encode::HexEncode(dlist_[i].toString(true)) + "\n").c_str(), fd);
        fputs((std::string("r_") + std::to_string(i) + ":" + common::Encode::HexEncode(Rx1_[i].toString(true)) + "\n").c_str(), fd);
        db_->Put("abe_c_" + std::to_string(i), clist_[i].toString(true));
        db_->Put("abe_d_" + std::to_string(i), dlist_[i].toString(true));
        db_->Put("abe_r_" + std::to_string(i), Rx1_[i].toString(true));
    }

    fclose(fd);
    db_->Put("abe_o", o_.toString());
    db_->Put("abe_c", C_.toString());
    db_->Put("abe_c1", C1_.toString(true));
    db_->Put("abe_l1", L1_.toString(true));
    db_->Put("abe_r1", R1_.toString(true));
    auto etime = common::TimeUtils::TimestampMs();
    CONTRACT_DEBUG("call use time: %d ms", etime - btime);
}

void PbcParing::DecryptData() {
    std::string o_str;
    if (db_->Get("abe_o", &o_str).ok()) {
        CONTRACT_ERROR("get data o failed!");
        return;
    }

    o_ = Zr(pairing_, (void*)o_str.c_str(), o_str.size());
    std::string c_str;
    if (db_->Get("abe_c", &c_str).ok()) {
        CONTRACT_ERROR("get data c failed!");
        return;
    }

    C_ = GT(pairing_, (void*)c_str.c_str(), c_str.size());
    std::string c1_str;
    if (db_->Get("abe_c1", &c1_str).ok()) {
        CONTRACT_ERROR("get data c1 failed!");
        return;
    }

    C1_ = G1(pairing_, (void*)c1_str.c_str(), c1_str.size());
    std::string l1_str;
    if (db_->Get("abe_l1", &l1_str).ok()) {
        CONTRACT_ERROR("get data l1 failed!");
        return;
    }

    L1_ = G1(pairing_, (void*)l1_str.c_str(), l1_str.size());
    std::string r1_str;
    if (db_->Get("abe_r1", &r1_str).ok()) {
        CONTRACT_ERROR("get data l1 failed!");
        return;
    }

    R1_ = G1(pairing_, (void*)r1_str.c_str(), r1_str.size());
    count_ = 20;
    clist_.clear();
    dlist_.clear();
    Rx1_.clear();
    for (int32_t i = 0; i < count_; i++) {
        std::string c_str;
        if (db_->Get("abe_c_" + std::to_string(i), &c_str).ok()) {
            CONTRACT_ERROR("get data c failed!");
            return;
        }

        clist_.push_back(G1(pairing_, (void*)c_str.c_str(), c_str.size()));
        std::string d_str;
        if (db_->Get("abe_d_" + std::to_string(i), &d_str).ok()) {
            CONTRACT_ERROR("get data d failed!");
            return;
        }

        dlist_.push_back(G1(pairing_, (void*)d_str.c_str(), d_str.size()));
        std::string r_str;
        if (db_->Get("abe_r_" + std::to_string(i), &r_str).ok()) {
            CONTRACT_ERROR("get data d failed!");
            return;
        }

        Rx1_.push_back(G1(pairing_, (void*)r_str.c_str(), r_str.size()));
    }

    Transform();
    Decrypt();
}

int PbcParing::SetupParams() {
    g_ = G1(pairing_, false);
    alpha_ = Zr(pairing_, true);
    a_ = Zr(pairing_, true);
    g1_ = GPP<G1>(pairing_, g_) ^ a_;
    auto tmp_gt = pairing_.apply(g_, g_);
    gt_ = GPP<GT>(pairing_, tmp_gt) ^ alpha_;
    for (int32_t i = 0; i < count_; ++i) {
        auto rand_zr = G1(pairing_, true);
        init_zrs_.push_back(rand_zr);
    }
    return kContractSuccess;
}

int PbcParing::KeyGen() {
    msk_ = GPP<G1>(pairing_, g_) ^ alpha_;
    s_ = Zr(pairing_, true);
    auto powg = GPP<G1>(pairing_, g_) ^ (a_ * s_);
    R_ = msk_ * powg;
    L_ = GPP<G1>(pairing_, g_) ^ s_;
    for (int32_t i = 0; i < count_; ++i) {
        auto pow_zr = GPP<G1>(pairing_, init_zrs_[i]) ^ s_;
        Rx_.push_back(pow_zr);
    }

    return kContractSuccess;
}

int PbcParing::GenTk() {
    o_ = Zr(pairing_, true);
    R1_ = GPP<G1>(pairing_, R_) ^ o_.inverse();
    L1_ = GPP<G1>(pairing_, L_) ^ o_.inverse();
    for (int32_t i = 0; i < count_; ++i) {
        auto pow_zr = GPP<G1>(pairing_, Rx_[i]) ^ o_.inverse();
        Rx1_.push_back(pow_zr);
    }

    return kContractSuccess;
}

int PbcParing::Encrypt() {
    k_ = GT(pairing_, true);
    r_ = Zr(pairing_, true);
    for (int32_t i = 0; i < count_; ++i) {
        tlist_.push_back(Zr(pairing_, true));
        llist_.push_back(Zr(pairing_, true));
    }

    auto tmp_gt = pairing_.apply(g_, g_);
    auto tmp_gt2 = GPP<GT>(pairing_, tmp_gt) ^ alpha_ ^ r_;
    C_ = k_ * tmp_gt2;
    C1_ = GPP<G1>(pairing_, g_) ^ r_;
    for (int32_t i = 0; i < count_; ++i) {
        auto rand_zr = Zr(pairing_, true);
        auto tmp_pow = GPP<Zr>(pairing_, rand_zr) ^ tlist_[i].inverse();
        auto mul = a_ * llist_[i] * tmp_pow;
        auto c_pow = GPP<G1>(pairing_, g_) ^ mul;
        clist_.push_back(c_pow);
        auto d_pow = GPP<G1>(pairing_, g_) ^ tlist_[i];
        dlist_.push_back(d_pow);
    }

    return kContractSuccess;
}

int PbcParing::Decrypt() {
    auto tmp_gt = GPP<GT>(pairing_, t_) ^ o_;
    decrypt_k_ = C_ * tmp_gt.inverse();
    return kContractSuccess;
}

int PbcParing::TransformAll() {
    GT total;
    for (int32_t i = 0; i < count_; ++i) {
        auto exp_w = Zr(pairing_, true);
        auto tmp_pow1 = GPP<G1>(pairing_, clist_[i]) ^ exp_w;
        auto tmp_pow2 = GPP<G1>(pairing_, dlist_[i]) ^ exp_w;
        auto tmp_gt1 = pairing_.apply(tmp_pow1, L1_);
        auto tmp_gt = pairing_.apply(tmp_pow2, Rx1_[i]);
        auto ti = tmp_gt1 * tmp_gt;
        trans_list_.push_back(tmp_gt1 * tmp_gt);
        if (i == 0) {
            total = tmp_gt;
        } else {
            total *= tmp_gt;
        }
    }

    t_ = pairing_.apply(C1_, R1_) / total;
    return kContractSuccess;
}

int PbcParing::Transform() {
//     GT total;
//     for (int32_t i = 0; i < 1; ++i) {
    int32_t i = 0;
    int32_t idx = 0;
    auto btime = common::TimeUtils::TimestampUs();
    auto exp_w = Zr(pairing_, true);
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_pow1 = GPP<G1>(pairing_, clist_[i]) ^ exp_w;
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_pow2 = GPP<G1>(pairing_, dlist_[i]) ^ exp_w;
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_gt1 = pairing_.apply(tmp_pow1, L1_);
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_gt = pairing_.apply(tmp_pow2, Rx1_[i]);
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto ti = tmp_gt1 * tmp_gt;
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
//         trans_list_.push_back(tmp_gt1 * tmp_gt);
//         if (i == 0) {
//             total = tmp_gt;
//         } else {
//             total *= tmp_gt;
//         }
//     }
// 
//     t_ = pairing_.apply(C1_, R1_) / total;
    return kContractSuccess;
}

}  // namespace contract

}  // namespace zjchain
