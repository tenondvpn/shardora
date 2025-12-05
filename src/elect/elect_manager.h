#pragma once

#include <mutex>
#include <memory>
#include <map>
#include <unordered_set>

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/tick.h"
#include "dht/base_dht.h"
#include "elect/elect_utils.h"
#include "elect/height_with_elect_blocks.h"
#include "network/shard_network.h"
#include "protos/elect.pb.h"
#include "protos/pools.pb.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace shardora {

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
        std::shared_ptr<block::AccountManager>& acc_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<db::Db>& db,
        NewElectBlockCallback new_elect_cb);
    ~ElectManager();
    int Init();
    uint64_t latest_height(uint32_t network_id);
    common::MembersPtr OnNewElectBlock(
        uint64_t height,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block,
        const std::shared_ptr<elect::protobuf::ElectBlock>& prev_elect_block);
    common::MembersPtr GetNetworkMembersWithHeight(
        uint64_t elect_height,
        uint32_t network_id,
        libff::alt_bn128_G2* common_pk,
        libff::alt_bn128_Fr* sec_key);

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void UpdatePrevElectMembers(
        const common::MembersPtr& members,
        protobuf::ElectBlock& elect_block,
        bool* elected,
        std::vector<std::string>* pkey_str_vect);
    bool ProcessPrevElectMembers(
        uint64_t height,
        protobuf::ElectBlock& elect_block,
        bool* elected,
        elect::protobuf::ElectBlock& prev_elect_block);
    void ProcessNewElectBlock(
        uint64_t height,
        protobuf::ElectBlock& elect_block,
        bool* elected);
    bool NodeHasElected(uint32_t network_id, const std::string& node_id);
    void ElectedToConsensusShard(
        protobuf::ElectBlock& elect_block,
        bool elected);

    static const uint64_t kWaitingHeartbeatPeriod = 20000000llu;

    // visit not frequently, just mutex lock
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    // mul thread access
    std::atomic<common::MembersPtr> members_ptr_[network::kConsensusShardEndNetworkId];
    std::atomic<common::MembersPtr> waiting_members_ptr_[network::kConsensusShardEndNetworkId];
    std::shared_ptr<HeightWithElectBlock> height_with_block_ = nullptr;
    std::atomic<uint64_t> elect_net_heights_map_[network::kConsensusShardEndNetworkId];

    // just new elect block thread
    std::map<uint32_t, ElectNodePtr> elect_network_map_;
    std::shared_ptr<ElectNode> elect_node_ptr_{ nullptr };
    std::unordered_set<uint64_t> added_height_[network::kConsensusShardEndNetworkId];
    std::set<std::string> prev_elected_ids_;
    std::set<std::string> now_elected_ids_;

    DISALLOW_COPY_AND_ASSIGN(ElectManager);
};

}  // namespace elect

}  // namespace shardora
