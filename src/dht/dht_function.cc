#include "dht/dht_function.h"

#include "common/encode.h"
#include "common/global_info.h"

namespace zjchain {

namespace dht {

int DhtFunction::GetDhtBucket(const std::string& src_dht_key, NodePtr& node) {
    int dht_bit_idx(0);
    while (dht_bit_idx != kDhtKeySize) {
        if (src_dht_key[dht_bit_idx] != node->dht_key[dht_bit_idx]) {
            std::bitset<8> holder_byte(static_cast<int>(src_dht_key[dht_bit_idx]));
            std::bitset<8> node_byte(static_cast<int>(node->dht_key[dht_bit_idx]));
            int bit_index(0);
            while (bit_index != 8U) {
                if (holder_byte[7U - bit_index] != node_byte[7U - bit_index]) {
                    break;
                }
                ++bit_index;
            }

            node->bucket = (8 * (kDhtKeySize - dht_bit_idx)) - bit_index - 1;
            return kDhtSuccess;
        }
        ++dht_bit_idx;
    }
    node->bucket = -1;
    return kDhtError;
}

uint32_t DhtFunction::PartialSort(const std::string& target, uint32_t count, Dht& dht) {
    uint32_t min_count = (std::min)(count, static_cast<uint32_t>(dht.size()));
    if (min_count <= 0) {
        return 0;
    }

    std::partial_sort(
            dht.begin(),
            dht.begin() + min_count,
            dht.end(),
            [target](const NodePtr& lhs, const NodePtr & rhs) {
        return CloserToTarget(lhs->dht_key, rhs->dht_key, target);
    });
    return min_count;
}

bool DhtFunction::CloserToTarget(
        const std::string& lhs,
        const std::string& rhs,
        const std::string& target) {
    for (uint32_t i = 0; i < kDhtKeySize; ++i) {
        unsigned char result1 = lhs[i] ^ target[i];
        unsigned char result2 = rhs[i] ^ target[i];
        if (result1 != result2) {
            return result1 < result2;
        }
    }
    return false;
}

bool DhtFunction::Displacement(
        const std::string& local_dht_key,
        Dht& dht,
        NodePtr& node,
        uint32_t& replace_pos) {
    std::map<uint32_t, unsigned int> bucket_rank_map;
    if (dht.size() < kDhtMaxNeighbors) {
        return true;
    }

    unsigned int max_bucket(0), max_bucket_count(1);
    std::for_each(
            std::begin(dht) + kDhtNearestNodesCount,
            std::end(dht),
        [&bucket_rank_map, &max_bucket, &max_bucket_count](const NodePtr& node_info) {
            auto bucket_iter(bucket_rank_map.find(node_info->bucket));
            if (bucket_iter != std::end(bucket_rank_map))
                (*bucket_iter).second++;
            else
                bucket_rank_map.insert(std::make_pair(node_info->bucket, 1));

            if (bucket_rank_map[node_info->bucket] >= max_bucket_count) {
                max_bucket = node_info->bucket;
                max_bucket_count = bucket_rank_map[node_info->bucket];
            }
    });

    if (max_bucket_count == 1) {
        auto iter = bucket_rank_map.find(node->bucket);
        if (iter == bucket_rank_map.end()) {
            return true;
        }
    }

    if ((max_bucket_count == 1) && (dht.back()->bucket < node->bucket)) {
        return false;
    }

    if (CloserToTarget(
            node->dht_key,
            dht.at(kDhtNearestNodesCount - 1)->dht_key,
            local_dht_key)) {
        replace_pos = kDhtNearestNodesCount - 1;
        return true;
    }

    for (int32_t index = dht.size() - 1; index >= 0; --index) {
        if (static_cast<unsigned int>(dht[index]->bucket) != max_bucket) {
            continue;
        }

        if ((dht[index]->bucket > node->bucket) || CloserToTarget(
                node->dht_key,
                dht[index]->dht_key,
                local_dht_key)) {
            replace_pos = index + 1;
            return true;
        }
    }
    return false;
}

int DhtFunction::IsClosest(
        const std::string& target,
        const std::string& local_dht_key,
        Dht& dht,
        bool& closest) {
    if (target == local_dht_key) {
        DHT_ERROR("target equal local nodeid, CloserToTarget goes wrong");
        return kDhtError;
    }

    if (target.size() != kDhtKeySize || local_dht_key.size() != kDhtKeySize) {
        DHT_ERROR("Invalid target_id passed. node id size[%d]", target.size());
        return kDhtError;
    }

    std::set<std::string> exclude;
    NodePtr closest_node(GetClosestNode(dht, target, local_dht_key, true, 1, exclude));
    if (closest_node == nullptr) {
        return kDhtError;
    }

    closest = (closest_node->bucket == -1) ||
            CloserToTarget(local_dht_key, closest_node->dht_key, target);
    return kDhtSuccess;
}

NodePtr DhtFunction::GetClosestNode(const Dht& dht, const std::string& target) {
    if (dht.size() == 0) {
        return nullptr;
    }

    if (dht.size() == 1) {
        return dht[0];
    }

    uint32_t min_pos = 0;
    if (CloserToTarget(dht[0]->dht_key, dht[1]->dht_key, target)) {
        min_pos = 0;
    } else {
        min_pos = 1;
    }

    for (uint32_t i = 2; i < dht.size(); ++i) {
        if (!CloserToTarget(dht[min_pos]->dht_key, dht[i]->dht_key, target)) {
            min_pos = i;
        }
    }

    return dht[min_pos];
}

void DhtFunction::GetNetworkNodes(
        const Dht& dht,
        uint32_t sharding_id,
        std::vector<NodePtr>& nodes) {
    for (uint32_t i = 0; i < dht.size(); ++i) {
        uint32_t tmp_id = DhtKeyManager::DhtKeyGetNetId(dht[i]->dht_key);
        if (tmp_id == sharding_id) {
            nodes.push_back(dht[i]);
        }
    }
}


NodePtr DhtFunction::GetClosestNode(
        Dht& dht,
        const std::string& target,
        const std::string& local_dht_key,
        bool not_self,
        uint32_t count,
        const std::set<std::string>& exclude) {
    auto closest_nodes(GetClosestNodes(dht, target, count));
    for (const auto& node_info : closest_nodes) {
        assert(node_info->dht_key != local_dht_key);
        auto iter = exclude.find(node_info->dht_key);
        if (iter != exclude.end()) {
            continue;
        }

        return node_info;
    }
    return nullptr;
}

std::vector<NodePtr> DhtFunction::GetClosestNodes(
        Dht& dht,
        const std::string& target,
        uint32_t number_to_get) {
    if (dht.empty()) {
        return std::vector<NodePtr>();
    }

    if (number_to_get == 0) {
        return std::vector<NodePtr>();
    }

    int sorted_count = PartialSort(target, number_to_get, dht);
    if (sorted_count == 0) {
        return std::vector<NodePtr>();
    }
    return std::vector<NodePtr>(dht.begin(), dht.begin() + static_cast<size_t>(sorted_count));
}

}  // namespace dht

}  // namespace zjchain
