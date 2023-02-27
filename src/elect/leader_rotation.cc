#include "elect/leader_rotation.h"

#include "common/global_info.h"
#include "dht/base_dht.h"
#include "network/dht_manager.h"
#include "network/route.h"

namespace zjchain {

namespace elect {

LeaderRotation::LeaderRotation(std::shared_ptr<security::Security>& security_ptr)
    : security_ptr_(security_ptr) {
    tick_.CutOff(1000000l * 60l, std::bind(&LeaderRotation::CheckRotation, this));
}

LeaderRotation::~LeaderRotation() {}

void LeaderRotation::OnElectBlock(const common::MembersPtr& members) {
    int32_t invalid_idx = (valid_idx_ + 1) % 2;
    rotation_item_[invalid_idx].max_pool_mod_num = 0;
    rotation_item_[invalid_idx].rotation_idx = 0;
    rotation_item_[invalid_idx].local_member = nullptr;
    rotation_item_[invalid_idx].valid_leaders.clear();
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        rotation_item_[invalid_idx].pool_leader_map[i] = nullptr;
    }

    for (auto iter = members->begin(); iter != members->end(); ++iter) {
        if ((*iter)->bls_publick_key == libff::alt_bn128_G2::zero()) {
            // should not to be leader
            (*iter)->valid_leader = false;
            continue;
        }

        (*iter)->valid_leader = true;
        if ((*iter)->id == security_ptr_->GetAddress()) {
            rotation_item_[invalid_idx].local_member = *iter;
        }

        if ((*iter)->pool_index_mod_num >= 0) {
            rotation_item_[invalid_idx].pool_leader_map[(*iter)->pool_index_mod_num] = *iter;
            if ((*iter)->pool_index_mod_num > rotation_item_[invalid_idx].max_pool_mod_num) {
                rotation_item_[invalid_idx].max_pool_mod_num = (*iter)->pool_index_mod_num;
            }

            rotation_item_[invalid_idx].valid_leaders.push_front(*iter);
        } else {
            rotation_item_[invalid_idx].valid_leaders.push_back(*iter);
        }
    }

    rotation_item_[invalid_idx].rotation_idx = rotation_item_[invalid_idx].max_pool_mod_num + 1;
    valid_idx_ = invalid_idx;
    std::lock_guard<std::mutex> guard(rotation_mutex_);
    cons_rotation_leaders_.clear();
    if (members->size() > 6) {
        check_rotation_ = true;
    }
}

int32_t LeaderRotation::GetThisNodeValidPoolModNum() {
    if (rotation_item_[valid_idx_].local_member == nullptr) {
        return -1;
    }

    return rotation_item_[valid_idx_].local_member->pool_index_mod_num;
}

void LeaderRotation::SendRotationReq(const std::string& id, int32_t pool_mod_num) {
    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        return;
    }

    transport::protobuf::Header msg;
    bool res = elect::ElectProto::CreateLeaderRotation(
        security_ptr_,
        dht->local_node(),
        id,
        pool_mod_num,
        msg);
    if (res) {
        network::Route::Instance()->Send(nullptr);
    }
}

void LeaderRotation::LeaderRotationReq(
        const protobuf::LeaderRotationMessage& leader_rotation,
        int32_t index,
        int32_t all_count) {
    std::lock_guard<std::mutex> guard(rotation_mutex_);
    std::string key = leader_rotation.leader_id() + "_" +
        std::to_string(leader_rotation.pool_mod_num());
    auto iter = cons_rotation_leaders_.find(key);
    if (iter == cons_rotation_leaders_.end()) {
        cons_rotation_leaders_[key] = std::set<int32_t>();
        cons_rotation_leaders_[key].insert(index);
    } else {
        iter->second.insert(index);
        if ((int32_t)iter->second.size() >= (all_count / 3 * 2 + 1)) {
            ChangeLeader(leader_rotation.leader_id(), leader_rotation.pool_mod_num());
        }
    }
}

void LeaderRotation::CheckRotation() {
    if (!check_rotation_) {
        tick_.CutOff(kCheckRotationPeriod, std::bind(&LeaderRotation::CheckRotation, this));
        return;
    }

    std::lock_guard<std::mutex> guard(rotation_mutex_);
    std::vector<int32_t> should_change_leaders;
    for (int32_t i = 0; i <= rotation_item_[valid_idx_].max_pool_mod_num; ++i) {
        bool change_leader = false;
        for (int32_t j = 0; j < (int32_t)common::kInvalidPoolIndex; ++j) {
            if (j % (rotation_item_[valid_idx_].max_pool_mod_num + 1) == i) {
                // if (bft::DispatchPool::Instance()->ShouldChangeLeader(j)) {
                //     change_leader = true;
                //     break;
                // }
            }
        }

        if (!change_leader) {
            continue;
        }

        should_change_leaders.push_back(i);
    }

    for (int32_t i = 0; i < (int32_t)should_change_leaders.size(); ++i) {
        auto new_leader = ChooseValidLeader(should_change_leaders[i]);
        if (new_leader == nullptr) {
            continue;
        }

        ELECT_WARN("check leader rotation: %d, %s, to: %s, this_node_pool_mod_num_: %d",
            should_change_leaders[i],
            common::Encode::HexEncode(rotation_item_[valid_idx_].pool_leader_map[should_change_leaders[i]]->id).c_str(),
            common::Encode::HexEncode(new_leader->id).c_str(),
            should_change_leaders[i]);
        SendRotationReq(new_leader->id, should_change_leaders[i]);
    }

    tick_.CutOff(kCheckRotationPeriod, std::bind(&LeaderRotation::CheckRotation, this));
}

void LeaderRotation::ChangeLeader(const std::string& id, int32_t pool_mod_num) {
    bool change_leader = false;
    for (int32_t j = 0; j < (int32_t)common::kInvalidPoolIndex; ++j) {
        if (j % (rotation_item_[valid_idx_].max_pool_mod_num + 1) == pool_mod_num) {
            // if (bft::DispatchPool::Instance()->ShouldChangeLeader(j)) {
            //     change_leader = true;
            //     break;
            // }
        }
    }

    if (!change_leader) {
        return;
    }

    common::BftMemberPtr new_leader = nullptr;
    for (int32_t i = 0; i < (int32_t)rotation_item_[valid_idx_].valid_leaders.size(); ++i) {
        if (rotation_item_[valid_idx_].valid_leaders[i]->id == id) {
            new_leader = rotation_item_[valid_idx_].valid_leaders[i];
            rotation_item_[valid_idx_].rotation_idx = i + 1;
            if (rotation_item_[valid_idx_].rotation_idx >=
                    (int32_t)rotation_item_[valid_idx_].valid_leaders.size()) {
                rotation_item_[valid_idx_].rotation_idx = 0;
            }

            break;
        }
    }

    if (new_leader == nullptr) {
        return;
    }

    rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->valid_leader = false;
    rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->pool_index_mod_num = -1;
    std::string src_id = rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->id;
    rotation_item_[valid_idx_].pool_leader_map[pool_mod_num] = new_leader;
    rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->pool_index_mod_num = pool_mod_num;
    std::string des_id = rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->id;
    ELECT_WARN("leader rotation: %d, %s, to: %s, this_node_pool_mod_num_: %d",
        pool_mod_num, common::Encode::HexEncode(src_id).c_str(),
        common::Encode::HexEncode(des_id).c_str(),
        rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->pool_index_mod_num);
    for (int32_t j = 0; j < (int32_t)common::kInvalidPoolIndex; ++j) {
        // if (j % (rotation_item_[valid_idx_].max_pool_mod_num + 1) == pool_mod_num) {
        //     bft::DispatchPool::Instance()->ChangeLeader(j);
        // }
    }
}

common::BftMemberPtr LeaderRotation::ChooseValidLeader(int32_t pool_mod_num) {
    int32_t start_idx = rotation_item_[valid_idx_].rotation_idx;
    for (int32_t i = rotation_item_[valid_idx_].rotation_idx;
            i < (int32_t)rotation_item_[valid_idx_].valid_leaders.size(); ++i) {
        if (!rotation_item_[valid_idx_].valid_leaders[i]->valid_leader) {
            continue;
        }

        if (rotation_item_[valid_idx_].valid_leaders[i]->id ==
                rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->id) {
            continue;
        }

        return rotation_item_[valid_idx_].valid_leaders[i];
    }

    for (int32_t i = 0; i < (int32_t)rotation_item_[valid_idx_].rotation_idx; ++i) {
        if (!rotation_item_[valid_idx_].valid_leaders[i]->valid_leader) {
            continue;
        }

        if (rotation_item_[valid_idx_].valid_leaders[i]->id ==
                rotation_item_[valid_idx_].pool_leader_map[pool_mod_num]->id) {
            continue;
        }

        return rotation_item_[valid_idx_].valid_leaders[i];
    }

    // TODO: no valid leader, then atavism reversion
    return nullptr;
}

};  // namespace elect

};  // namespace zjchain
