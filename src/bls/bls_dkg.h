#pragma once

#include <atomic>
#include <memory>
#include <random>
#include <unordered_map>
#include <set>

#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>
#include <dkg/dkg.h>

#include "bls/bls_utils.h"
#include "common/bitmap.h"
#include "common/node_members.h"
#include "common/tick.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "db/db.h"
#include "dht/dht_utils.h"
#include "protos/bls.pb.h"
#include "protos/elect.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace bls {

class BlsManager;
class BlsDkg {
public:
    BlsDkg();
    ~BlsDkg();
    void Init(
        BlsManager* bls_mgr,
        std::shared_ptr<security::Security>& security,
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const libff::alt_bn128_G2 local_publick_key,
        const libff::alt_bn128_G2 common_public_key,
        std::shared_ptr<db::Db>& db);
    void OnNewElectionBlock(
        uint64_t elect_height,
        common::MembersPtr& members,
        std::shared_ptr<TimeBlockItem>& latest_timeblock_info);
    void HandleMessage(const transport::MessagePtr& header);
    uint64_t elect_hegiht() {
        return elect_hegiht_;
    }

    const libff::alt_bn128_G2& local_publick_key() const {
        return local_publick_key_;
    }
    const libff::alt_bn128_G2& common_public_key() const {
        return common_public_key_;
    }

    uint32_t t() const {
        return min_aggree_member_count_;
    }

    uint32_t n() const {
        return member_count_;
    }

    void Destroy();

    void TimerMessage(const transport::MessagePtr& msg_ptr);

private:
    void HandleVerifyBroadcast(const transport::MessagePtr& header);
    void HandleSwapSecKey(const transport::MessagePtr& header);
    void HandleCheckVerifyReq(const transport::MessagePtr& header);
    void HandleCheckSwapKeyReq(const transport::MessagePtr& header);
    bool IsSignValid(const transport::MessagePtr& msg_ptr, std::string* msg_hash);
    void BroadcastVerfify(uint8_t thread_idx);
    void SwapSecKey(uint8_t thread_idx);
    void FinishNoLock(uint8_t thread_idx);
    void CreateContribution(uint32_t valid_n, uint32_t valid_t);
    void CreateDkgMessage(transport::MessagePtr& msg_ptr);
    void BroadcastFinish(uint8_t thread_idx, const common::Bitmap& bitmap);
    void CreateSwapKey(uint32_t member_idx, std::string* seckey, int32_t* seckey_len);
    void CheckVerifyAllValid(uint8_t thread_idx);
    void SendGetVerifyInfo(uint8_t thread_idx, int32_t index);
    void CheckSwapKeyAllValid(uint8_t thread_idx);
    void SendGetSwapKey(uint8_t thread_idx, int32_t index);
    libff::alt_bn128_G2 GetVerifyG2FromDb(uint32_t first_index, uint32_t* changed_idx);
    void DumpLocalPrivateKey();
    bool VerifySekkeyValid(uint32_t idx, uint32_t peer_index, const libff::alt_bn128_Fr& seckey);
    bool IsVerifyBrdPeriod() {
#ifdef ZJC_UNITTEST
        return true;
#endif
        auto now_tm_us = common::TimeUtils::TimestampUs();
        if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4)) {
            return true;
        }

        return false;
    }

    bool IsSwapKeyPeriod() {
#ifdef ZJC_UNITTEST
        return true;
#endif
        auto now_tm_us = common::TimeUtils::TimestampUs();
        if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 7) &&
                now_tm_us >= (begin_time_us_ + kDkgPeriodUs * 4)) {
            return true;
        }

        return false;
    }

    bool IsFinishPeriod() {
#ifdef ZJC_UNITTEST
        return true;
#endif
        auto now_tm_us = common::TimeUtils::TimestampUs();
        if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 10) &&
            now_tm_us >= (begin_time_us_ + kDkgPeriodUs * 7)) {
            return true;
        }

        return false;
    }

    static const uint64_t kTimeBlsPeriodSeconds = common::kTimeBlockCreatePeriodSeconds / 3;

    BlsManager* bls_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;

    int64_t kDkgPeriodUs = kTimeBlsPeriodSeconds / 10 * 1000u * 1000u;
    common::MembersPtr members_{ nullptr };
    uint64_t elect_hegiht_{ 0 };
    std::vector<libff::alt_bn128_Fr> local_src_secret_key_contribution_;
    uint32_t local_member_index_{ common::kInvalidUint32 };
    std::shared_ptr<libBLS::Dkg> dkg_instance_;
    std::set<uint32_t> invalid_node_map_[common::kEachShardMaxNodeCount];
    uint32_t min_aggree_member_count_{ 0 };
    uint32_t member_count_{ 0 };
    libff::alt_bn128_Fr local_sec_key_;
    libff::alt_bn128_G2 local_publick_key_;
    libff::alt_bn128_G2 common_public_key_;
    std::shared_ptr<std::mt19937> random_ptr_;
    bool finished_{ false };
    uint32_t valid_sec_key_count_{ 0 };
    std::unordered_map<std::string, std::shared_ptr<MaxBlsMemberItem>> max_bls_members_;
    std::string max_finish_hash_;
    uint32_t max_finish_count_{ 0 };
    std::unordered_set<uint32_t> valid_swapkey_set_;
    bool valid_swaped_keys_[common::kEachShardMaxNodeCount];
    bool has_swaped_keys_[common::kEachShardMaxNodeCount];
    uint64_t begin_time_us_{ 0 };
    std::unordered_map<int32_t, std::string> verify_map_;
    std::unordered_map<int32_t, std::string> swap_key_map_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    int32_t max_member_count_ = 1024;
    int32_t max_agree_count_ = 1024 * 2 / 3 + 1;
    uint64_t ver_offset_ = 0;
    uint64_t swap_offset_ = 0;
    uint64_t finish_offset_ = 0;
    bool has_broadcast_verify_ = false;
    bool has_broadcast_swapkey_ = false;
    bool has_finished_ = false;

#ifdef ZJC_UNITTEST
    transport::MessagePtr ver_brd_msg_;
    transport::MessagePtr sec_swap_msgs_;
    std::vector<libff::alt_bn128_G2> g2_vec_;
#endif
    DISALLOW_COPY_AND_ASSIGN(BlsDkg);
};

};  // namespace bls

};  // namespace zjchain
