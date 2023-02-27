#pragma once

#include <string>
#include <bitset>

#include "common/utils.h"
#include "dht/dht_utils.h"
#include "dht/dht_utils.h"
#include "dht/base_dht.h"

namespace zjchain {

namespace dht {

class DhtFunction {
public:
    static int GetDhtBucket(const std::string& src_dht_key, NodePtr& node);
    static uint32_t PartialSort(const std::string& target, uint32_t count, Dht& dht);
    static bool CloserToTarget(
            const std::string& lhs,
            const std::string& rhs,
            const std::string& target);
    static bool Displacement(
            const std::string& target,
            Dht& dht,
            NodePtr& node,
            uint32_t& replace_pos);
    static int IsClosest(
            const std::string& target,
            const std::string& local_dht_key,
            Dht& dht,
            bool& closest);
    static NodePtr GetClosestNode(
            Dht& dht,
            const std::string& target,
            const std::string& local_dht_key,
            bool not_self,
            uint32_t count,
            const std::set<std::string>& exclude);
    static std::vector<NodePtr> GetClosestNodes(
            Dht& dht,
            const std::string& target,
            uint32_t number_to_get);

private:
    DhtFunction() {}
    ~DhtFunction() {}

    DISALLOW_COPY_AND_ASSIGN(DhtFunction);
};

}  // namespace dht

}  // namespace zjchain
