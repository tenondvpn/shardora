#pragma once

#include <unordered_set>

#include <common/time_utils.h>
#include <sstream>
#include <string>

#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>

#include <common/hash.h>
#include <consensus/hotstuff/utils.h>
#include <protos/block.pb.h>
#include <protos/view_block.pb.h>
#include <protos/prefix_db.h>
#include "network/network_utils.h"
#include <tools/utils.h>

namespace shardora {

namespace hotstuff {

static const uint64_t ORPHAN_BLOCK_TIMEOUT_US = 10000000lu;

typedef uint64_t View;
typedef std::string HashStr;

// ViewDuration Init Params.
static const uint64_t ViewDurationSampleSize = 10;
static const double ViewDurationStartTimeoutMs = 300;
static const double ViewDurationMaxTimeoutMs = 60000;
static const double ViewDurationMultiplier = 1.3; // The cost of causing a freeze after a large election is high. Once it gets stuck, the recovery time is long (e.g., inconsistent leaders). If it is too small, the CPU will not be able to come down for a long time when there are no transactions.


enum Status : int {
  kSuccess = 0,
  kError = 1,
  kNotFound = 2,
  kInvalidArgument = 3,
  kBlsVerifyWaiting = 4,
  kBlsVerifyFailed = 5,
  kAcceptorTxsEmpty = 6,
  kAcceptorBlockInvalid = 7,
  kOldView = 8,
  kElectItemNotFound = 9,
  kWrapperTxsEmpty = 10,
  kBlsHandled = 11,
  kTxRepeated = 12,
  kLackOfParentBlock = 13,
  kNotExpectHash = 14,
  kInvalidOpposedCount = 15,
  kLeaderInvalid = 16,
};

enum WaitingBlockType {
    kRootBlock,
    kSyncBlock,
    kToBlock,
};

HashStr GetQCMsgHash(const view_block::protobuf::QcItem& qc_item);
HashStr GetTCMsgHash(const view_block::protobuf::QcItem &tc_item);
bool IsQcTcValid(const view_block::protobuf::QcItem& qc_item);
// HashStr GetViewBlockHash(const view_block::protobuf::ViewBlockItem&
// view_block_item);

namespace {
// libff bigint(const char*) asserts unless the string is decimal digits only.
inline bool IsLibffDecimalFieldString(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}
} // namespace

// Both aggregated and unaggregated signatures share the same structure.
struct AggregateSignature {
    libff::alt_bn128_G1 sig_;
    // Because aggregate signatures are used, public keys need to be aggregated, so it is necessary to know who the participants are
    std::unordered_set<uint32_t> participants_; // member indexes who submit signatures.

    AggregateSignature() : sig_(libff::alt_bn128_G1::zero()) {}
    
    AggregateSignature(
            const libff::alt_bn128_G1& sig,
            const std::unordered_set<uint32_t>& parts) : sig_(sig), participants_(parts) {}

    inline std::unordered_set<uint32_t> participants() const {
        return participants_;
    }

    inline libff::alt_bn128_G1 signature() const {
        return sig_;
    }

    void set_signature(libff::alt_bn128_G1 g1_sig) {
        sig_ = g1_sig;
    }

    void add_participant(uint32_t member_idx) {
        participants_.insert(member_idx);
    }

    inline bool IsValid() const {
        // minimum participants size is 1.
        return !sig_.is_zero() && participants_.size() > 0;
    }

    bool LoadFromProto(const view_block::protobuf::AggregateSig& agg_sig_proto) {
        sig_ = libff::alt_bn128_G1::zero();
        participants_.clear();
        try {
            const std::string& sx = agg_sig_proto.sign_x();
            if (!sx.empty()) {
                if (!IsLibffDecimalFieldString(sx)) {
                    SHARDORA_ERROR("load from proto failed, invalid sign_x");
                    sig_ = libff::alt_bn128_G1::zero();
                    return false;
                }
                sig_.X = libff::alt_bn128_Fq(sx.c_str());
            }
            const std::string& sy = agg_sig_proto.sign_y();
            if (!sy.empty()) {
                if (!IsLibffDecimalFieldString(sy)) {
                    SHARDORA_ERROR("load from proto failed, invalid sign_y");
                    sig_ = libff::alt_bn128_G1::zero();
                    return false;
                }
                sig_.Y = libff::alt_bn128_Fq(sy.c_str());
            }
            const std::string& sz = agg_sig_proto.sign_z();
            if (!sz.empty()) {
                if (!IsLibffDecimalFieldString(sz)) {
                    SHARDORA_ERROR("load from proto failed, invalid sign_z");
                    sig_ = libff::alt_bn128_G1::zero();
                    return false;
                }
                sig_.Z = libff::alt_bn128_Fq(sz.c_str());
            }
        } catch (const std::exception& e) {   
            SHARDORA_ERROR("load from proto failed, err: %s", e.what());
            sig_ = libff::alt_bn128_G1::zero();
            return false;
        } catch (...) {
            SHARDORA_ERROR("load from proto failed, unknown err");
            sig_ = libff::alt_bn128_G1::zero();
            return false;
        }

        for (const auto& par : agg_sig_proto.participants()) {
            participants_.insert(par);
        }
        
        return true;        
    }

    view_block::protobuf::AggregateSig DumpToProto() const {
        auto agg_sig_proto = view_block::protobuf::AggregateSig();

        agg_sig_proto.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(sig_.X));
        agg_sig_proto.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(sig_.Y));
        agg_sig_proto.set_sign_z(libBLS::ThresholdUtils::fieldElementToString(sig_.Z));

        for (const auto& par : participants_) {
            agg_sig_proto.add_participants(par);
        }
        
        return agg_sig_proto;        
    }
};


// Consensus statistics in this elect height
struct MemberConsensusStat {
    uint32_t succ_num; // Number of successful consensuses
    uint32_t fail_num; // Number of failed consensuses

    MemberConsensusStat() {
        succ_num = 0;
        fail_num = 0;
    }

    MemberConsensusStat(uint16_t succ_num, uint16_t fail_num) : succ_num(succ_num), fail_num(fail_num) {}

    inline HashStr GetHash() {
        std::stringstream ss;
        ss << succ_num << fail_num;
        return common::Hash::keccak256(ss.str());
    }
};

using ViewBlock = view_block::protobuf::ViewBlockItem;
using QC = view_block::protobuf::QcItem;
using TC = QC;

// For Fast HotStuff.
struct AggregateQC {
    std::unordered_map<uint32_t, std::shared_ptr<QC>> qcs_;
    std::shared_ptr<AggregateSignature> sig_;
    View view_;

    AggregateQC(
            const std::unordered_map<uint32_t, std::shared_ptr<QC>>& qcs,
            const std::shared_ptr<AggregateSignature>& sig,
            View view) :
        qcs_(qcs), sig_(sig), view_(view) {}

    inline std::unordered_map<uint32_t, std::shared_ptr<QC>> QCs() const {
        return qcs_;
    }

    inline std::shared_ptr<AggregateSignature> Sig() const {
        return sig_;
    }

    inline View GetView() const {
        return view_;
    }

    inline bool IsValid() const {
        return sig_->IsValid() && sig_->participants().size() == qcs_.size();  
    }
};

struct SyncInfo : public std::enable_shared_from_this<SyncInfo> {
    std::shared_ptr<QC> qc;
    std::shared_ptr<TC> tc;
    std::shared_ptr<AggregateQC> agg_qc;
    
    SyncInfo() : qc(nullptr), tc(nullptr) {};

    std::shared_ptr<SyncInfo> WithQC(const std::shared_ptr<QC>& q) {
        qc = q;
        return shared_from_this();
    }

    std::shared_ptr<SyncInfo> WithTC(const std::shared_ptr<TC>& t) {
        tc = t;
        return shared_from_this();
    }

    std::shared_ptr<SyncInfo> WithAggQC(const std::shared_ptr<AggregateQC>& a) {
        agg_qc = a;
        return shared_from_this();
    }    
};

std::shared_ptr<SyncInfo> new_sync_info();

} // namespace hotstuff

} // namespace shardora
