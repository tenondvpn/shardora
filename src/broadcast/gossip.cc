#include "broadcast/gossip.h"

#include <cmath>
#include <algorithm>
#include <functional>

#include "broadcast/broadcast_utils.h"
#include "common/global_info.h"
#include "common/random.h"
#include "dht/base_dht.h"

namespace zjchain {

namespace broadcast {

Gossip::Gossip() {
    for (uint32_t i = 0; i < 102400u; ++i) {
        pos_vec_.push_back(i);
    }

    std::random_shuffle(pos_vec_.begin(), pos_vec_.end());
    RandomPos(0);
}

Gossip::~Gossip() {}

void Gossip::Broadcasting(
        uint8_t thread_idx,
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr) {
    assert(dht_ptr);
//     assert(!dht_ptr->readonly_hash_sort_dht()->empty());
    auto& message = msg_ptr->header;
    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    std::vector<dht::NodePtr> nodes;
    uint32_t neighbor_count = GetNeighborCount(message);
    bool got[readobly_dht->size()] = { false };
    uint32_t valid_pos = random_poses_[prev_pos_++ % kMaxRamdonPos];
    while (true) {
        auto pos = pos_vec_[valid_pos++ % pos_vec_.size()] % readobly_dht->size();
        if (got[pos]) {
            continue;
        }

        got[pos] = true;
        nodes.push_back((*readobly_dht)[pos]);
        if (nodes.size() >= neighbor_count || nodes.size() >= readobly_dht->size()) {
            break;
        }
    }

    ZJC_DEBUG("gossip Broadcasting: %lu, size: %u, dht size: %d, des net: %d",
        msg_ptr->header.hash64(), nodes.size(), dht_ptr->readonly_hash_sort_dht()->size(), dht_ptr->local_node()->sharding_id);
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        int res = transport::TcpTransport::Instance()->Send(
            thread_idx,
            nodes[i]->public_ip,
            nodes[i]->public_port,
            msg_ptr->header);
        BROAD_DEBUG("broadcast random send to: %s:%d, txhash: %lu, res: %u",
            nodes[i]->public_ip.c_str(),
            nodes[i]->public_port,
            msg_ptr->header.hash64(),
            res);
    }
}

void Gossip::RandomPos(uint8_t thread_idx) {
    for (uint32_t i = 0; i < kMaxRamdonPos; ++i) {
        random_poses_[i] = common::Random::RandomUint32();
    }

    tick_.CutOff(3000000, std::bind(&Gossip::RandomPos, this, std::placeholders::_1));
}

}  // namespace broadcast

}  // namespace zjchain
