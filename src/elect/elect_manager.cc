#include "elect/elect_manager.h"

#include <bls/bls_utils.h>
#include <functional>

#include "block/block_manager.h"
#include "bls/BLSPublicKey.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/time_utils.h"
#include "dht/dht_utils.h"
#include "db/db_utils.h"
#include "elect/elect_proto.h"
#include "elect/elect_pledge.h"
#include "network/route.h"
#include "network/shard_network.h"
#include "vss/vss_manager.h"
#include <protos/prefix_db.h>

namespace shardora {

namespace elect {

int ElectManager::Init() {
//     for (uint32_t i = network::kRootCongressNetworkId;
//             i < network::kConsensusShardEndNetworkId; ++i) {
//         auto block_ptr = elect_block_mgr_.GetLatestElectBlock(i);
//         if (block_ptr == nullptr) {
//             break;
//         }
// 
//         OnNewElectBlock(
//             0,
//             block_ptr->elect_height(),
//             block_ptr);
//         vss::VssManager::Instance()->OnElectBlock(
//             elect_block.shard_network_id(),
//             latest_elect_block_height);
//     }

    return kElectSuccess;
}

ElectManager::ElectManager(
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& acc_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<db::Db>& db,
        NewElectBlockCallback new_elect_cb) {
    vss_mgr_ = vss_mgr;
    acc_mgr_ = acc_mgr;
    block_mgr_ = block_mgr;
    security_ = security;
    db_ = db;
    new_elect_cb_ = new_elect_cb;
    elect_block_mgr_.Init(db_);
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    height_with_block_ = std::make_shared<HeightWithElectBlock>(security, db_);
    bls_mgr_ = bls_mgr;
//     network::Route::Instance()->RegisterMessage(
//         common::kElectMessage,
//         std::bind(&ElectManager::HandleMessage, this, std::placeholders::_1));
    memset(latest_leader_count_, 0, sizeof(latest_leader_count_));
    memset(latest_member_count_, 0, sizeof(latest_member_count_));
    for (uint32_t i = 0; i < network::kConsensusShardEndNetworkId; ++i) {
        elect_net_heights_map_[i] = common::kInvalidUint64;
    }

    ELECT_DEBUG("TTTTTTTTT ElectManager RegisterMessage called!");
}

ElectManager::~ElectManager() {}

int ElectManager::Join(uint32_t network_id) {
    auto iter = elect_network_map_.find(network_id);
    if (iter != elect_network_map_.end()) {
        ELECT_INFO("this node has join network[%u]", network_id);
        return kElectNetworkJoined;
    }

    elect_node_ptr_ = std::make_shared<ElectNode>(
        network_id,
        std::bind(
            &ElectManager::NodeHasElected,
            this,
            std::placeholders::_1,
            std::placeholders::_2),
        security_,
        prefix_db_);
    if (elect_node_ptr_->Init(acc_mgr_) != network::kNetworkSuccess) {
        ELECT_ERROR("node join network [%u] failed!", network_id);
        return kElectError;
    }

    elect_network_map_[network_id] = elect_node_ptr_;
    CHECK_MEMORY_SIZE(elect_network_map_);
    return kElectSuccess;
}

int ElectManager::Quit(uint32_t network_id) {
    ElectNodePtr elect_node = nullptr;
    {
        std::lock_guard<std::mutex> guard(elect_network_map_mutex_);
        auto iter = elect_network_map_.find(network_id);
        if (iter == elect_network_map_.end()) {
            ELECT_INFO("this node has join network[%u]", network_id);
            return kElectNetworkNotJoined;
        }

        elect_node = iter->second;
        elect_network_map_.erase(iter);
        CHECK_MEMORY_SIZE(elect_network_map_);
    }

    elect_node->Destroy();
    return kElectSuccess;
}

void ElectManager::OnTimeBlock(uint64_t tm_block_tm) {
}

void ElectManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    assert(false);
}

common::MembersPtr ElectManager::OnNewElectBlock(
        uint64_t height,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block_ptr,
        const std::shared_ptr<elect::protobuf::ElectBlock>& prev_elect_block_ptr,
        db::DbWriteBatch& db_batch) {
    auto& elect_block = *elect_block_ptr;
    if (elect_block.shard_network_id() >= network::kConsensusShardEndNetworkId ||
            elect_block.shard_network_id() < network::kRootCongressNetworkId) {
        ZJC_DEBUG("elect block sharding id invalid: %u", elect_block.shard_network_id());
        return nullptr;
    }

    if (max_sharding_id_ < elect_block.shard_network_id()) {
        max_sharding_id_ = elect_block.shard_network_id();
    }

    bool elected = false;
    now_elected_ids_.clear();
    bool cons_elect_valid = ProcessPrevElectMembers(
        height,
        elect_block,
        &elected,
        *prev_elect_block_ptr,
        db_batch);
    ProcessNewElectBlock(height, elect_block, &elected);
    if (!cons_elect_valid && !elected) {
        if (common::GlobalInfo::Instance()->network_id() == elect_block.shard_network_id()) {
            elected = true;
        }
    }

    ElectedToConsensusShard(elect_block, elected);
    elect_block_mgr_.OnNewElectBlock(height, elect_block, db_batch);
    // assert(members_ptr_[elect_block.shard_network_id()] != nullptr);
    return members_ptr_[elect_block.shard_network_id()];
//     if (new_elect_cb_ != nullptr) {
//         new_elect_cb_(
//             elect_block.shard_network_id(),
//             height,
//             members_ptr_[elect_block.shard_network_id()],
//             elect_block_ptr);
//     }
}

void ElectManager::ElectedToConsensusShard(
        protobuf::ElectBlock& elect_block,
        bool cons_elected) {
    auto local_netid = common::GlobalInfo::Instance()->network_id();
    ZJC_DEBUG("now join network local sharding id: %u, elect sharding id: %u, elected: %d",
        local_netid, elect_block.shard_network_id(), cons_elected);
    if (!cons_elected) {
        if (local_netid == elect_block.shard_network_id()) {
//             Quit(local_netid);
            if (Join(local_netid + network::kConsensusWaitingShardOffset) != kElectSuccess) {
                ELECT_ERROR("join elected network failed![%u]",
                    local_netid + network::kConsensusWaitingShardOffset);
            } else {
                ELECT_INFO("join new election shard network: %u",
                    local_netid + network::kConsensusWaitingShardOffset);
            }
            common::GlobalInfo::Instance()->set_network_id(
                local_netid + network::kConsensusWaitingShardOffset);
        }
    } else {
        if (local_netid != elect_block.shard_network_id()) {
//             Quit(local_netid);
            if (Join(elect_block.shard_network_id()) != kElectSuccess) {
                ELECT_ERROR("join elected network failed![%u]", elect_block.shard_network_id());
            } else {
                ELECT_INFO("join new election shard network: %u", elect_block.shard_network_id());
            }
            common::GlobalInfo::Instance()->set_network_id(elect_block.shard_network_id());
        } else {
            std::vector<std::string> erase_nodes;
            for (auto iter = prev_elected_ids_.begin(); iter != prev_elected_ids_.end(); ++iter) {
                if (now_elected_ids_.find(*iter) != now_elected_ids_.end()) {
                    continue;
                }

                erase_nodes.push_back(*iter);
            }

            auto dht = network::DhtManager::Instance()->GetDht(local_netid);
            if (dht != nullptr) {
                dht->Drop(erase_nodes);
            }

            prev_elected_ids_ = now_elected_ids_;
            if (Join(elect_block.shard_network_id()) != kElectSuccess) {
                ELECT_WARN("join elected network failed![%u]", elect_block.shard_network_id());
            } else {
                ELECT_INFO("join new election shard network: %u", elect_block.shard_network_id());
            }
        }
    }
}

bool ElectManager::ProcessPrevElectMembers(
        uint64_t height,
        protobuf::ElectBlock& elect_block,
        bool* elected,
        elect::protobuf::ElectBlock& prev_elect_block,
        db::DbWriteBatch& db_batch) {
    if (!elect_block.has_prev_members() || elect_block.prev_members().prev_elect_height() <= 0) {
        ELECT_DEBUG("not has prev members. has: %d. pre elect height: %lu, shard: %u, height: %lu",
            elect_block.has_prev_members(),
            elect_block.prev_members().prev_elect_height(),
            elect_block.shard_network_id(),
            elect_block.elect_height());
//         assert(false);
        return false;
    }

    if (prev_elect_block.in_size() <= 0) {
        ZJC_DEBUG("prev elect block in size error.");
        return false;
    }

    ZJC_DEBUG("now handle block in size success.");
    auto& added_heights = added_height_[elect_block.shard_network_id()];
    if (added_heights.find(elect_block.prev_members().prev_elect_height()) != added_heights.end()) {
        ELECT_ERROR("height has added: %lu", elect_block.prev_members().prev_elect_height());
        return false;
    }

    added_heights.insert(elect_block.prev_members().prev_elect_height());
    latest_member_count_[prev_elect_block.shard_network_id()] = prev_elect_block.in_size();
    std::map<uint32_t, uint32_t> begin_index_map;
    auto& in = prev_elect_block.in();
//     std::cout << "in member count: " << in.size() << std::endl;
    auto shard_members_ptr = std::make_shared<common::Members>();
    ClearExistsNetwork(prev_elect_block.shard_network_id());
    auto& prev_members_bls = elect_block.prev_members().bls_pubkey();
    if (prev_members_bls.size() != in.size()) {
        ELECT_ERROR("prev_members_bls.size(): %d, in.size(): %d, height: %lu",
            prev_members_bls.size(),
            in.size(),
            elect_block.prev_members().prev_elect_height());
        assert(false);
        return false;
    }

    uint32_t expect_leader_count = (int32_t)pow(
        2.0,
        (double)((int32_t)log2(double(in.size() / 3))));
    if (expect_leader_count > common::kImmutablePoolSize) {
        expect_leader_count = common::kImmutablePoolSize;
    }

    uint32_t leader_count = 0;
    for (int32_t i = 0; i < in.size(); ++i) {
        auto id = security_->GetAddress(in[i].pubkey());
        int32_t pool_idx_mod_num = leader_count;  // elect_block.prev_members().bls_pubkey(i).pool_idx_mod_num();
        if (leader_count >= expect_leader_count) {
            pool_idx_mod_num = -1;
        } else {
            ++leader_count;
        }

        auto agg_bls_pk = bls::Proto2BlsPublicKey(in[i].agg_bls_pk());
        auto agg_bls_pk_proof = bls::Proto2BlsPopProof(in[i].agg_bls_pk_proof());
        shard_members_ptr->push_back(std::make_shared<common::BftMember>(
            prev_elect_block.shard_network_id(),
            id,
            in[i].pubkey(),
            i,
            pool_idx_mod_num,
            *agg_bls_pk,
            *agg_bls_pk_proof));
        now_elected_ids_.insert(id);
        AddNewNodeWithIdAndIp(prev_elect_block.shard_network_id(), id);
    }

    latest_leader_count_[prev_elect_block.shard_network_id()] = expect_leader_count;
    std::vector<std::string> pk_vec;
    UpdatePrevElectMembers(shard_members_ptr, elect_block, elected, &pk_vec);
    auto common_pk = BLSPublicKey(std::make_shared<std::vector<std::string>>(pk_vec));
    bool local_node_is_super_leader = false;
    for (auto iter = shard_members_ptr->begin(); iter != shard_members_ptr->end(); ++iter) {
        ELECT_WARN("DDDDDDDDDD now height: %lu, now elect height: %lu, "
            "elect height: %lu, network: %d,"
            "leader: %s, pool_index_mod_num: %d, valid pk: %d",
            height,
            elect_block.elect_height(),
            elect_block.prev_members().prev_elect_height(),
            prev_elect_block.shard_network_id(),
            common::Encode::HexEncode((*iter)->id).c_str(),
            (*iter)->pool_index_mod_num,
            ((*iter)->bls_publick_key == libff::alt_bn128_G2::zero()));
    }

    if (*elected) {
        for (auto iter = shard_members_ptr->begin();
                iter != shard_members_ptr->end(); ++iter) {
            if ((*iter)->id != security_->GetAddress()) {
                security_->GetEcdhKey(
                    (*iter)->pubkey,
                    &(*iter)->backup_ecdh_key);
            }
        }

        for (auto iter = shard_members_ptr->begin();
                iter != shard_members_ptr->end(); ++iter) {
            if ((*iter)->id != security_->GetAddress()) {
                security_->GetEcdhKey(
                    (*iter)->pubkey,
                    &(*iter)->leader_ecdh_key);
            }
        }
    }

    members_ptr_[prev_elect_block.shard_network_id()] = shard_members_ptr;
    height_with_block_->AddNewHeightBlock(
        elect_block.prev_members().prev_elect_height(),
        prev_elect_block.shard_network_id(),
        shard_members_ptr,
        *common_pk.getPublicKey());
    if (elect_net_heights_map_[prev_elect_block.shard_network_id()] == common::kInvalidUint64 ||
            elect_block.prev_members().prev_elect_height() >
            elect_net_heights_map_[prev_elect_block.shard_network_id()]) {
        elect_net_heights_map_[prev_elect_block.shard_network_id()] =
            elect_block.prev_members().prev_elect_height();

        {// hack 质押合约中 nowElectHeight 字段 
         // src/contract/tests/contracts/ElectPlegde/ElectPledgeContract.sol
            auto plege_addr = elect::ElectPlege::gen_elect_plege_contract_addr(prev_elect_block.shard_network_id());
            prefix_db_->AddNowElectHeight2Plege(plege_addr, elect_block.prev_members().prev_elect_height(), db_batch);
        }
        ELECT_DEBUG("set netid: %d, elect height: %lu",
            prev_elect_block.shard_network_id(), elect_block.prev_members().prev_elect_height());
    }

    if (prev_elect_block.shard_network_id() == common::GlobalInfo::Instance()->network_id() ||
            (prev_elect_block.shard_network_id() + network::kConsensusWaitingShardOffset) ==
            common::GlobalInfo::Instance()->network_id() || *elected) {
        ELECT_INFO("set netid: %d, elect height: %lu, now net: %d, elected: %d",
            prev_elect_block.shard_network_id(),
            elect_block.prev_members().prev_elect_height(),
            common::GlobalInfo::Instance()->network_id(),
            *elected);
    }

    local_node_is_super_leader_ = local_node_is_super_leader;
    return true;
}

void ElectManager::ProcessNewElectBlock(
        uint64_t height,
        protobuf::ElectBlock& elect_block,
        bool* elected) {
    auto& in = elect_block.in();
    auto shard_members_ptr = std::make_shared<common::Members>();
    if (elect_block.shard_network_id() == common::GlobalInfo::Instance()->network_id()) {
        local_waiting_node_member_index_ = kInvalidMemberIndex;
    }

    for (int32_t i = 0; i < in.size(); ++i) {
        auto id = security_->GetAddress(in[i].pubkey());
        auto agg_bls_pk = bls::Proto2BlsPublicKey(in[i].agg_bls_pk());
        auto agg_bls_pk_proof = bls::Proto2BlsPopProof(in[i].agg_bls_pk_proof());
        shard_members_ptr->push_back(std::make_shared<common::BftMember>(
            elect_block.shard_network_id(),
            id,
            in[i].pubkey(),
            i,
            in[i].pool_idx_mod_num(),
            *agg_bls_pk,
            *agg_bls_pk_proof));
        AddNewNodeWithIdAndIp(elect_block.shard_network_id(), id);
        if (id == security_->GetAddress()) {
            *elected = true;
            local_waiting_node_member_index_ = i;
        }

        now_elected_ids_.insert(id);
        ELECT_WARN("FFFFFFFF ProcessNewElectBlock network: %d, "
            "elect height: %lu, pre elect height: %lu"
            "member leader: %s,, (*iter)->pool_index_mod_num: %d, "
            "local_waiting_node_member_index_: %d",
            elect_block.shard_network_id(),
            height,
            elect_block.prev_members().prev_elect_height(),
            common::Encode::HexEncode(id).c_str(),
            in[i].pool_idx_mod_num(),
            local_waiting_node_member_index_);
    }

    waiting_members_ptr_[elect_block.shard_network_id()] = shard_members_ptr;
    waiting_elect_height_[elect_block.shard_network_id()] = height;
}

void ElectManager::UpdatePrevElectMembers(
        const common::MembersPtr& members,
        protobuf::ElectBlock& elect_block,
        bool* elected,
        std::vector<std::string>* pkey_str_vect) {
//     std::cout << "DDDDDDDDDDDD " << members->size() << ":" << (uint32_t)elect_block.prev_members().bls_pubkey_size() << std::endl;
    if (members->size() != (uint32_t)elect_block.prev_members().bls_pubkey_size()) {
        return;
    }

    auto t = common::GetSignerCount(members->size());
    int32_t i = 0;
    int32_t local_member_index = kInvalidMemberIndex;
    for (auto iter = members->begin(); iter != members->end(); ++iter, ++i) {
        if ((*iter)->id == security_->GetAddress()) {
            local_member_index = i;
            *elected = true;
        }

        if (elect_block.prev_members().bls_pubkey(i).x_c0().empty()) {
            (*iter)->bls_publick_key = libff::alt_bn128_G2::zero();
            ELECT_DEBUG("get invalid bls public key index: %d, id: %s, elect height: %lu",
                i, common::Encode::HexEncode((*iter)->id).c_str(), elect_block.prev_members().prev_elect_height());
            continue;
        }

        std::vector<std::string> pkey_str = {
            elect_block.prev_members().bls_pubkey(i).x_c0(),
            elect_block.prev_members().bls_pubkey(i).x_c1(),
            elect_block.prev_members().bls_pubkey(i).y_c0(),
            elect_block.prev_members().bls_pubkey(i).y_c1()
        };

        BLS_DEBUG("id: %s, elected: %d, pk: %s,%s,%s,%s",
            common::Encode::HexEncode((*iter)->id).c_str(),
            *elected,
            elect_block.prev_members().bls_pubkey(i).x_c0().c_str(),
            elect_block.prev_members().bls_pubkey(i).x_c1().c_str(),
            elect_block.prev_members().bls_pubkey(i).y_c0().c_str(),
            elect_block.prev_members().bls_pubkey(i).y_c1().c_str());
//         std::cout << "set bls public key: " << i << ", " << elect_block.prev_members().bls_pubkey(i).x_c0()
//             << ", " << elect_block.prev_members().bls_pubkey(i).x_c1()
//             << ", " << elect_block.prev_members().bls_pubkey(i).y_c0()
//             << ", " << elect_block.prev_members().bls_pubkey(i).y_c1()
//             << std::endl;
        BLSPublicKey pkey(std::make_shared<std::vector<std::string>>(pkey_str));
        (*iter)->bls_publick_key = *pkey.getPublicKey();
    }

    *pkey_str_vect = std::vector<std::string>{
            elect_block.prev_members().common_pubkey().x_c0(),
            elect_block.prev_members().common_pubkey().x_c1(),
            elect_block.prev_members().common_pubkey().y_c0(),
            elect_block.prev_members().common_pubkey().y_c1()
    };

//     std::cout << "set common public key: " << i << ", " << elect_block.prev_members().common_pubkey().x_c0()
//         << ", " << elect_block.prev_members().common_pubkey().x_c1()
//         << ", " << elect_block.prev_members().common_pubkey().y_c0()
//         << ", " << elect_block.prev_members().common_pubkey().y_c1()
//         << std::endl;

    auto common_pk = BLSPublicKey(std::make_shared<std::vector<std::string>>(*pkey_str_vect));
    if (*elected) {
        bls_mgr_->SetUsedElectionBlock(
            elect_block.prev_members().prev_elect_height(),
            elect_block.shard_network_id(),
            members->size(),
            *common_pk.getPublicKey());
//         ELECT_DEBUG("use common public key: %s, %s, %s, %s",
//             elect_block.prev_members().common_pubkey().x_c0().c_str(),
//             elect_block.prev_members().common_pubkey().x_c1().c_str(),
//             elect_block.prev_members().common_pubkey().y_c0().c_str(),
//             elect_block.prev_members().common_pubkey().y_c1().c_str());
    }

    if (*elected) {
        local_node_member_index_ = local_member_index;
    }
}

uint64_t ElectManager::latest_height(uint32_t network_id) {
    if (network_id >= network::kConsensusShardEndNetworkId) {
        return common::kInvalidUint64;
    }

    return elect_net_heights_map_[network_id];
}

common::MembersPtr ElectManager::GetNetworkMembersWithHeight(
        uint64_t elect_height,
        uint32_t network_id,
        libff::alt_bn128_G2* common_pk,
        libff::alt_bn128_Fr* sec_key) {
    if (elect_height == 0) {
        assert(false);
        return nullptr;
    }
    
    return height_with_block_->GetMembersPtr(
        security_, elect_height, network_id, common_pk, sec_key);
}

uint32_t ElectManager::GetMemberCountWithHeight(uint64_t elect_height, uint32_t network_id) {
    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    auto members_ptr = GetNetworkMembersWithHeight(
        elect_height,
        network_id,
        &common_pk,
        &sec_key);
    if (members_ptr != nullptr) {
        return members_ptr->size();
    }

    return 0;
}

common::MembersPtr ElectManager::GetNetworkMembers(uint32_t network_id) {
    return members_ptr_[network_id];
}

common::MembersPtr ElectManager::GetWaitingNetworkMembers(uint32_t network_id) {
    return waiting_members_ptr_[network_id];
}

bool ElectManager::NodeHasElected(uint32_t network_id, const std::string& node_id) {
    if (network_id < network::kRootCongressNetworkId ||
            network_id >= network::kConsensusShardEndNetworkId) {
        return false;
    }

    auto valid_members = members_ptr_[network_id];
    if (valid_members != nullptr) {
        for (auto iter = valid_members->begin(); iter != valid_members->end(); ++iter) {
            if ((*iter)->id == node_id) {
                ZJC_DEBUG("sharding id: %u, has elected: %s",
                    network_id, common::Encode::HexEncode(node_id).c_str());
                return true;
            }
        }
    }

    auto waiting_members = waiting_members_ptr_[network_id];
    if (waiting_members != nullptr) {
        for (auto iter = waiting_members->begin(); iter != waiting_members->end(); ++iter) {
            if ((*iter)->id == node_id) {
                ZJC_DEBUG("waiting sharding id: %u, has elected: %s",
                    network_id, common::Encode::HexEncode(node_id).c_str());
                return true;
            }
        }
    }

    return false;
}

common::BftMemberPtr ElectManager::GetMember(uint32_t network_id, uint32_t index) {
    if (network_id >= network::kConsensusShardEndNetworkId) {
        return nullptr;
    }

    auto mems_ptr = members_ptr_[network_id];
    if (mems_ptr == nullptr) {
        return nullptr;
    }

    if (index >= mems_ptr->size()) {
        return nullptr;
    }

    return (*mems_ptr)[index];
}

uint32_t ElectManager::GetMemberCount(uint32_t network_id) {
    return latest_member_count_[network_id];
}

int32_t ElectManager::GetNetworkLeaderCount(uint32_t network_id) {
    return latest_leader_count_[network_id];
}

bool ElectManager::IsIdExistsInAnyShard(const std::string& id) {
    for (uint32_t i = network::kRootCongressNetworkId; i <= max_sharding_id_; ++i) {
        auto iter = added_net_id_set_.find(i);
        if (iter != added_net_id_set_.end()) {
            return iter->second.find(id) != iter->second.end();
        }
    }
    

    return false;
}

// bool ElectManager::IsIpExistsInAnyShard(uint32_t network_id, const std::string& ip) {
//     std::lock_guard<std::mutex> guard(added_net_ip_set_mutex_);
//     auto iter = added_net_ip_set_.find(network_id);
//     if (iter != added_net_id_set_.end()) {
//         return iter->second.find(ip) != iter->second.end();
//     }
// 
//     return false;
// }

void ElectManager::ClearExistsNetwork(uint32_t network_id) {
    {
        std::lock_guard<std::mutex> guard(added_net_id_set_mutex_);
        added_net_id_set_[network_id].clear();
    }

    {
        std::lock_guard<std::mutex> guard(added_net_ip_set_mutex_);
        added_net_ip_set_[network_id].clear();
    }
}

void ElectManager::AddNewNodeWithIdAndIp(
        uint32_t network_id,
        const std::string& id) {
    std::lock_guard<std::mutex> guard(added_net_id_set_mutex_);
    added_net_id_set_[network_id].insert(id);
    CHECK_MEMORY_SIZE(added_net_id_set_[network_id]);
}

}  // namespace elect

}  // namespace shardora
