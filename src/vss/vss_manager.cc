#include "vss/vss_manager.h"

#include "common/time_utils.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/get_proto_hash.h"
#include "transport/processor.h"

namespace shardora {

namespace vss {

VssManager::VssManager() {}

void VssManager::OnTimeBlock(const std::shared_ptr<view_block::protobuf::ViewBlockItem>& block) {
    epoch_random_ = common::Hash::Hash64(block->qc().sign_x() + block->qc().sign_y());
}

}  // namespace vss

}  // namespace shardora
