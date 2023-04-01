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
#include "elect/elect_pool_manager.h"
#include "elect/member_manager.h"
#include "elect/nodes_stoke_manager.h"
#include "elect/height_with_elect_blocks.h"
#include "elect/elect_node_detail.h"
#include "elect/leader_rotation.h"
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
    int CreateElectTransaction(
        uint32_t shard_netid,
        uint64_t final_statistic_block_height,
        const block::protobuf::BlockTx& src_tx_info,
        pools::protobuf::TxMessage& tx_info);
    int BackupCheckElectionBlockTx(
        const block::protobuf::BlockTx& local_tx_info,
        const block::protobuf::BlockTx& tx_info);
    void OnTimeBlock(uint64_t tm_block_tm);
    void OnNewElectBlock(
        uint8_t thread_idx,
        uint64_t height,
        protobuf::ElectBlock& elect_block);
    int GetElectionTxInfo(block::protobuf::BlockTx& tx_info);
    std::shared_ptr<elect::protobuf::ElectBlock> GetLatestElectBlock(uint32_t sharding_id) {
        return elect_block_mgr_.GetLatestElectBlock(sharding_id);
    }

    common::MembersPtr GetNetworkMembersWithHeight(
        uint64_t elect_height,
        uint32_t network_id,
        libff::alt_bn128_G2* common_pk,
        libff::alt_bn128_Fr* sec_key);
    uint32_t GetMemberCountWithHeight(uint64_t elect_height, uint32_t network_id);
    uint32_t GetMemberIndex(uint32_t network_id, const std::string& node_id);
    common::MembersPtr GetNetworkMembers(uint32_t network_id);
    common::BftMemberPtr GetMemberWithId(uint32_t network_id, const std::string& node_id);
    common::BftMemberPtr GetMember(uint32_t network_id, uint32_t index);
    uint32_t GetMemberCount(uint32_t network_id);
    int32_t GetNetworkLeaderCount(uint32_t network_id);
    std::shared_ptr<MemberManager> GetMemberManager(uint32_t network_id);
    common::MembersPtr GetWaitingNetworkMembers(uint32_t network_id);
    bool IsIdExistsInAnyShard(uint32_t network_id, const std::string& id);
//     bool IsIpExistsInAnyShard(uint32_t network_id, const std::string& ip);

    // ip::IpWeight GetIpWeight(uint64_t height, uint32_t network_id) {
    //     return height_with_block_.GetIpWeight(height, network_id);
    // }

    libff::alt_bn128_G2 GetCommonPublicKey(uint64_t height, uint32_t network_id) {
        return height_with_block_->GetCommonPublicKey(height, network_id);
    }

    std::unordered_set<std::string> leaders(uint32_t network_id) {
        std::lock_guard<std::mutex> guard(network_leaders_mutex_);
        auto iter = network_leaders_.find(network_id);
        if (iter != network_leaders_.end()) {
            return iter->second;
        }

        return {};
    }
    
    bool IsSuperLeader(uint32_t network_id, const std::string& id) {
        std::lock_guard<std::mutex> guard(network_leaders_mutex_);
        auto iter = network_leaders_.find(network_id);
        if (iter != network_leaders_.end()) {
            return iter->second.find(id) != iter->second.end();
        }

        return false;
    }

    int32_t local_node_pool_mod_num() {
        return leader_rotation_->GetThisNodeValidPoolModNum();
    }

    int32_t local_node_member_index() {
        return local_node_member_index_;
    }

    common::BftMemberPtr local_mem_ptr(uint32_t network_id) {
        return leader_rotation_->local_member();
    }

    std::unordered_set<uint32_t> valid_shard_networks() {
        std::lock_guard<std::mutex> guard(valid_shard_networks_mutex_);
        return valid_shard_networks_;
    }

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
    void WaitingNodeSendHeartbeat();
    void AddNewNodeWithIdAndIp(uint32_t network_id, const std::string& id, uint32_t ip);
    void ClearExistsNetwork(uint32_t network_id);
    void UpdatePrevElectMembers(
        const common::MembersPtr& members,
        protobuf::ElectBlock& elect_block,
        bool* elected,
        std::vector<std::string>* pkey_str_vect);
    bool ProcessPrevElectMembers(
        protobuf::ElectBlock& elect_block,
        bool* elected);
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
    std::shared_ptr<ElectPoolManager> pool_manager_ = nullptr;
    common::Tick create_elect_block_tick_;
    std::unordered_set<uint64_t> added_height_;
    uint64_t elect_net_heights_map_[network::kConsensusShardEndNetworkId];
    std::mutex elect_members_mutex_;
    std::unordered_map<uint32_t, std::unordered_set<std::string>> network_leaders_;
    std::mutex network_leaders_mutex_;
    std::unordered_set<uint32_t> valid_shard_networks_;
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
    std::shared_ptr<MemberManager> mem_manager_ptr_[network::kConsensusShardEndNetworkId];
    int32_t latest_member_count_[network::kConsensusShardEndNetworkId];
    int32_t latest_leader_count_[network::kConsensusShardEndNetworkId];
    elect::NodeIndexMapPtr node_index_map_[network::kConsensusShardEndNetworkId];
    std::shared_ptr<HeightWithElectBlock> height_with_block_ = nullptr;
    common::BftMemberPtr pool_mod_leaders_[common::kInvalidPoolIndex];
    std::set<std::string> prev_elected_ids_;
    std::set<std::string> now_elected_ids_;
    std::shared_ptr<LeaderRotation> leader_rotation_ = nullptr;
    bool local_node_is_super_leader_{ false };
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<NodesStokeManager> stoke_mgr_ = nullptr;
    ElectBlockManager elect_block_mgr_;
    std::shared_ptr<db::Db> db_ = nullptr;
    NewElectBlockCallback new_elect_cb_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ElectManager);
};

}  // namespace elect

}  // namespace zjchain
