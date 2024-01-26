#pragma once

#include <condition_variable>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "common/node_members.h"
#include "dht/base_dht.h"
#include "dht/dht_utils.h"
#include "protos/network.pb.h"

namespace zjchain {

namespace network {

class Universal : public dht::BaseDht {
public:
    Universal(dht::NodePtr& local_node, std::shared_ptr<db::Db>& db);
    virtual ~Universal();
    virtual int Init(
        std::shared_ptr<security::Security>& security,
        dht::BootstrapResponseCallback boot_cb,
        dht::NewNodeJoinCallback node_join_cb);
    virtual int Join(dht::NodePtr& node);
    virtual int Destroy();
    virtual bool CheckDestination(const std::string& des_dht_key, bool closest);
    virtual void HandleMessage(const transport::MessagePtr& msg);
    virtual bool IsUniversal() { return true; }
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block);
    void AddNetworkId(uint32_t network_id);
    void RemoveNetworkId(uint32_t network_id);
    bool HasNetworkId(uint32_t network_id);
    std::vector<dht::NodePtr> LocalGetNetworkNodes(uint32_t network_id, uint32_t count);

private:
    struct ElectItem {
        uint64_t height;
        std::unordered_set<std::string> id_set;
    };

    void ProcessGetNetworkNodesRequest(const transport::MessagePtr& header);
    void ProcessGetNetworkNodesResponse(const transport::MessagePtr& header);
    int AddNodeToUniversal(dht::NodePtr& node);

    bool* universal_ids_{ nullptr };
    std::condition_variable wait_con_;
    std::mutex wait_mutex_;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::vector<dht::NodePtr> wait_nodes_;
    std::unordered_map<int32_t, std::shared_ptr<ElectItem>> sharding_latest_height_map_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Universal);
};

}  // namespace network

}  //namespace zjchain
