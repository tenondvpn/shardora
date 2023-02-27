#pragma once

#include <vector>
#include <deque>

#include "common/time_utils.h"
#include "common/tick.h"
#include "elect/elect_utils.h"
#include "elect/elect_node_detail.h"
#include "elect/elect_proto.h"
#include "protos/elect.pb.h"
#include "security/security.h"

namespace zjchain {

namespace elect {

class LeaderRotation {
public:
    LeaderRotation(std::shared_ptr<security::Security>& security_ptr);
    ~LeaderRotation();
    void OnElectBlock(const common::MembersPtr& members);
    int32_t GetThisNodeValidPoolModNum();
    void LeaderRotationReq(
        const protobuf::LeaderRotationMessage& leader_rotation,
        int32_t index,
        int32_t all_count);
    common::BftMemberPtr local_member() {
        return rotation_item_[valid_idx_].local_member;
    }

private:
    struct RotationItem {
        common::BftMemberPtr pool_leader_map[common::kInvalidPoolIndex];
        std::deque<common::BftMemberPtr> valid_leaders;
        int32_t max_pool_mod_num;
        int32_t rotation_idx;
        common::BftMemberPtr local_member{ nullptr };
    };

    void CheckRotation();
    common::BftMemberPtr ChooseValidLeader(int32_t pool_mod_num);
    void SendRotationReq(const std::string& id, int32_t pool_mod_num);
    void ChangeLeader(const std::string& id, int32_t pool_mod_num);

    static const int64_t kCheckRotationPeriod{ 5000000l };
    std::unordered_map<std::string, std::set<int32_t>> cons_rotation_leaders_;

    RotationItem rotation_item_[2];
    int32_t valid_idx_{ 0 };
    common::Tick tick_;
    std::mutex rotation_mutex_;
    bool check_rotation_{ false };
    std::shared_ptr<security::Security> security_ptr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(LeaderRotation);
};

};  // namespace elect

};  // namespace zjchain
