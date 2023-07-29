#pragma once

#include <mutex>
#include <memory>
#include <map>
#include <unordered_set>

#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/tick.h"
#include "dht/base_dht.h"
#include "elect/elect_block_manager.h"
#include "elect/elect_utils.h"
#include "elect/height_with_elect_blocks.h"
#include "network/shard_network.h"
#include "protos/elect.pb.h"
#include "protos/pools.pb.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace vss {
    class VssManager;
}

namespace elect {

typedef network::ShardNetwork<dht::BaseDht> ElectNode;
typedef std::shared_ptr<ElectNode> ElectNodePtr;

class ElectManager {
public:
    ElectManager(
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<db::Db>& db,
        NewElectBlockCallback new_elect_cb);
    ~ElectManager();
    int Init();
    int Join(uint8_t thread_idx, uint32_t network_id);
    int Quit(uint32_t network_id);
    uint64_t latest_height(uint32_t network_id);
    void OnTimeBlock(uint64_t tm_block_tm);
    common::MembersPtr OnNewElectBlock(
        uint8_t thread_idx,
        uint64_t height,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block,
        const std::shared_ptr<elect::protobuf::ElectBlock>& prev_elect_block,
        db::DbWriteBatch& db_batch);
    std::shared_ptr<elect::protobuf::ElectBlock> GetLatestElectBlock(uint32_t sharding_id) {
        return elect_block_mgr_.GetLatestElectBlock(sharding_id);
    }

    common::MembersPtr GetNetworkMembersWithHeight(
        uint64_t elect_height,
        uint32_t network_id,
        libff::alt_bn128_G2* common_pk,
        libff::alt_bn128_Fr* sec_key);
    uint32_t GetMemberCountWithHeight(uint64_t elect_height, uint32_t network_id);
    common::MembersPtr GetNetworkMembers(uint32_t network_id);
    common::BftMemberPtr GetMemberWithId(uint32_t network_id, const std::string& node_id);
    common::BftMemberPtr GetMember(uint32_t network_id, uint32_t index);
    uint32_t GetMemberCount(uint32_t network_id);
    int32_t GetNetworkLeaderCount(uint32_t network_id);
    common::MembersPtr GetWaitingNetworkMembers(uint32_t network_id);
    bool IsIdExistsInAnyShard(const std::string& id);

    uint64_t waiting_elect_height(uint32_t network_id) {
        return waiting_elect_height_[network_id];
    }

    bool local_node_is_super_leader() {
        return local_node_is_super_leader_;
    }

    int32_t local_waiting_node_member_index() const {
        return local_waiting_node_member_index_;
    }

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void AddNewNodeWithIdAndIp(uint32_t network_id, const std::string& id);
    void ClearExistsNetwork(uint32_t network_id);
    void UpdatePrevElectMembers(
        const common::MembersPtr& members,
        protobuf::ElectBlock& elect_block,
        bool* elected,
        std::vector<std::string>* pkey_str_vect);
    bool ProcessPrevElectMembers(
        protobuf::ElectBlock& elect_block,
        bool* elected,
        elect::protobuf::ElectBlock& prev_elect_block,
        db::DbWriteBatch& db_batch);
    void ProcessNewElectBlock(
        uint64_t height,
        protobuf::ElectBlock& elect_block,
        bool* elected);
    bool NodeHasElected(uint32_t network_id, const std::string& node_id);
    void ElectedToConsensusShard(
        uint8_t thread_idx,
        protobuf::ElectBlock& elect_block,
        bool elected);

    static const uint64_t kWaitingHeartbeatPeriod = 20000000llu;

    // visit not frequently, just mutex lock
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::map<uint32_t, ElectNodePtr> elect_network_map_;
    std::mutex elect_network_map_mutex_;
    std::shared_ptr<ElectNode> elect_node_ptr_{ nullptr };
    common::Tick create_elect_block_tick_;
    std::unordered_set<uint64_t> added_height_[network::kConsensusShardEndNetworkId];
    uint64_t elect_net_heights_map_[network::kConsensusShardEndNetworkId];
    std::mutex elect_members_mutex_;
    std::mutex network_leaders_mutex_;
    std::mutex valid_shard_networks_mutex_;
    common::Tick waiting_hb_tick_;
    std::unordered_map<uint32_t, std::unordered_set<std::string>> added_net_id_set_;
    std::mutex added_net_id_set_mutex_;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> added_net_ip_set_;
    std::mutex added_net_ip_set_mutex_;
    volatile int32_t local_node_member_index_{ kInvalidMemberIndex };
    volatile int32_t local_waiting_node_member_index_{ kInvalidMemberIndex };
    common::MembersPtr members_ptr_[network::kConsensusShardEndNetworkId];
    common::MembersPtr waiting_members_ptr_[network::kConsensusShardEndNetworkId];
    uint64_t waiting_elect_height_[network::kConsensusShardEndNetworkId];
    int32_t latest_member_count_[network::kConsensusShardEndNetworkId];
    int32_t latest_leader_count_[network::kConsensusShardEndNetworkId];
    std::shared_ptr<HeightWithElectBlock> height_with_block_ = nullptr;
    common::BftMemberPtr pool_mod_leaders_[common::kInvalidPoolIndex];
    std::set<std::string> prev_elected_ids_;
    std::set<std::string> now_elected_ids_;
    bool local_node_is_super_leader_{ false };
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    ElectBlockManager elect_block_mgr_;
    std::shared_ptr<db::Db> db_ = nullptr;
    NewElectBlockCallback new_elect_cb_ = nullptr;
    uint32_t max_sharding_id_ = 3;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ElectManager);
};

}  // namespace elect

}  // namespace zjchain
