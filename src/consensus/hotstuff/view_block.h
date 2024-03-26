#pragma once

#include <protos/block.pb.h>

namespace shardora {
namespace consensus {

class ViewBlock {
public:
    

private:
    std::string hash_;
    std::string parent_;

    uint32_t leader_idx_;
    std::shared_ptr<block::protobuf::Block> block_;
    std::vector<common::HashValue> tx_hash_list_;

    
};
    
}
}
