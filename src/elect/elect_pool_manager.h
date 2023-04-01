#pragma once

#include <random>
#include <map>

#include "common/bitmap.h"
#include "common/tick.h"
#include "elect/elect_pool.h"
#include "elect/elect_waiting_nodes.h"
#include "elect/node_history_credit.h"
#include "protos/elect.pb.h"
#include "protos/block.pb.h"
#include "security/security.h"

namespace zjchain {

namespace vss {
    class VssManager;
}

namespace elect {

class ElectManager;
class ElectPoolManager {
public:
    ElectPoolManager(
        ElectManager* elect_mgr,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<NodesStokeManager>& stoke_mgr,
        std::shared_ptr<db::Db>& db);
    ~ElectPoolManager();
    void NetworkMemberChange(uint32_t network_id, common::MembersPtr& members_ptr);
    void AddWaitingPoolNode(uint32_t network_id, NodeDetailPtr& node_ptr);
    void UpdateNodeInfoWithBlock(const block::protobuf::Block& block_info);
    int BackupCheckElectionBlockTx(
        const block::protobuf::BlockTx& local_tx_info,
        const block::protobuf::BlockTx& tx_info);
    void UpdateWaitingNodes(
        const protobuf::WaitingNodesMessage& waiting_nodes,
        const std::string& root_node_id,
        const common::BloomFilter& nodes_filter);
    int CreateElectTransaction(
        uint32_t shard_netid,
        uint64_t final_statistic_block_height,
        const block::protobuf::BlockTx& src_tx_info,
        pools::protobuf::TxMessage& tx_info);
    void OnTimeBlock(uint64_t tm_block_tm);
    int GetElectionTxInfo(block::protobuf::BlockTx& tx_info);
    void OnNewElectBlock(
        uint64_t height,
        protobuf::ElectBlock& elect_block);
    void UpdateNodesStoke();

private:
    int GetAllTxInfoBloomFiler(
        const block::protobuf::BlockTx& tx_info,
        common::BloomFilter* cons_all,
        common::BloomFilter* cons_weed_out,
        common::BloomFilter* pick_all,
        common::BloomFilter* pick_in,
        elect::protobuf::ElectBlock* ec_block);
    int GetAllBloomFilerAndNodes(
        const elect::protobuf::StatisticInfo& statistic_info,
        uint32_t shard_netid,
        common::BloomFilter* cons_all,
        common::BloomFilter* cons_weed_out,
        common::BloomFilter* pick_all,
        common::BloomFilter* pick_in,
        std::vector<NodeDetailPtr>& elected_nodes,
        std::set<std::string>& weed_out_vec);
    void FtsGetNodes(
        uint32_t shard_netid,
        bool weed_out,
        uint32_t count,
        common::BloomFilter* nodes_filter,
        const std::vector<NodeDetailPtr>& src_nodes,
        std::set<int32_t>& res_nodes);
    void SmoothFtsValue(
        uint32_t shard_netid,
        int32_t count,
        std::mt19937_64& g2,
        std::vector<NodeDetailPtr>& src_nodes);
    void GetMiniTopNInvalidNodes(
        uint32_t network_id,
        const elect::protobuf::StatisticInfo& statistic_info,
        uint32_t count,
        std::map<int32_t, uint32_t>* nodes);
    void GetInvalidLeaders(
        uint32_t network_id,
        const elect::protobuf::StatisticInfo& statistic_info,
        std::map<int32_t, uint32_t>* nodes);
    int SelectLeader(
        uint32_t network_id,
        const common::Bitmap& bitmap,
        elect::protobuf::ElectBlock* ec_block);

    std::shared_ptr<db::Db> db_ = nullptr;
    NodeHistoryCredit node_credit_;
    ElectManager* elect_mgr_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptrl
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<NodesStokeManager> stoke_mgr_ = nullptr;
    std::unordered_map<uint32_t, ElectPoolPtr> elect_pool_map_;
    std::mutex elect_pool_map_mutex_;
    // one ip just one node
    std::unordered_set<uint32_t> node_ip_set_;
    std::mutex node_ip_set_mutex_;
    std::unordered_map<std::string, NodeDetailPtr> all_node_map_;
    std::mutex all_node_map_mutex_;
    std::unordered_map<uint32_t, ElectWaitingNodesPtr> waiting_pool_map_;
    std::mutex waiting_pool_map_mutex_;
    uint32_t updated_net_id_{ common::kInvalidUint32 };
    common::Tick update_stoke_tick_;

    DISALLOW_COPY_AND_ASSIGN(ElectPoolManager);
};

};  // namespace elect

};  //  namespace zjchain
