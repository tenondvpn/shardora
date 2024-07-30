#pragma once

#include "common/utils.h"
#include "network/network_utils.h"

namespace shardora {

namespace network {

enum class ShardStatus : uint8_t {
    kClosed = 0,
    kPreopened = 1,
    kOpened = 2,
};


class NetInfo {
public:
    NetInfo() : status_(ShardStatus::kClosed), net_id_(common::kInvalidUint32) {}
    NetInfo(uint32_t net_id) {        
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        net_id_ = net_id;

        if (net_id < network::kInitOpenedShardCount + network::kConsensusShardBeginNetworkId) {
            status_ = ShardStatus::kOpened;
        } else {
            status_ = ShardStatus::kClosed;
        }
    }
    
    ~NetInfo() {};

    const inline ShardStatus Status() {
        return status_;
    }

    inline bool IsValidShard() {
        return net_id_ >= network::kConsensusShardBeginNetworkId && net_id_ < network::kConsensusShardEndNetworkId;
    }
    
    inline bool IsClosed() {
        return status_ == ShardStatus::kClosed;
    }
    
    inline bool IsPreopened() {
        return status_ == ShardStatus::kPreopened;
    }
    
    inline bool IsOpened() {
        return status_ == ShardStatus::kOpened;
    }
    
    bool SetPreopened() {
        if (!IsClosed()) {
            return false;
        }
        status_ = ShardStatus::kPreopened;
        return true;
    }
    
    bool SetOpened() {
        if (!IsPreopened()) {
            return false;
        }
        status_ = ShardStatus::kOpened;
        return true;
    }

private:
    ShardStatus status_;
    uint32_t net_id_;
};

class NetsInfo {
public:
    static NetsInfo* Instance() {
        static NetsInfo instance;
        return &instance;        
    }

    void Init() {
        for (uint32_t net_id = 0; net_id < network::kConsensusShardEndNetworkId; net_id++) {
            net_infos_[net_id] = std::make_shared<NetInfo>(net_id);
        }

        biggest_opened_net_id_ = network::kInitOpenedShardCount + network::kConsensusShardBeginNetworkId - 1;
        cur_preopened_net_id_ = 0;
    }

    inline uint32_t BiggestOpenedNetId() {
        return biggest_opened_net_id_;
    }

    inline bool AllOpened() {
        return biggest_opened_net_id_ == network::kConsensusShardEndNetworkId - 1;
    }

    inline uint32_t PreopenedNetworkId() {
        return cur_preopened_net_id_;
    }

    inline bool HasPreopenedNetwork() {
        return cur_preopened_net_id_ != 0;
    }

    std::shared_ptr<NetInfo> net_info(uint32_t net_id) {
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id >= network::kConsensusShardEndNetworkId) {
            return std::make_shared<NetInfo>(common::kInvalidUint32);
        }
        
        return net_infos_[net_id];
    }

    inline bool IsClosed(uint32_t net_id) {
        return net_info(net_id)->IsClosed();
    }
    
    inline bool IsPreopened(uint32_t net_id) {
        return net_info(net_id)->IsPreopened();
    }
    
    inline bool IsOpened(uint32_t net_id) {
        return net_info(net_id)->IsOpened();
    }    

    void SetPreopened(uint32_t net_id) {
        bool ok = net_info(net_id)->SetPreopened();
        if (ok) {
            cur_preopened_net_id_ = net_id;
        }
    }
    
    void SetOpened(uint32_t net_id) {
        bool ok = net_info(net_id)->SetOpened();
        if (ok) {
            cur_preopened_net_id_ = 0;
            if (net_id > biggest_opened_net_id_) {
                biggest_opened_net_id_ = net_id;
            }
        }
    }
    
private:
    std::shared_ptr<NetInfo> net_infos_[network::kConsensusShardEndNetworkId];
    uint32_t cur_preopened_net_id_;
    uint32_t biggest_opened_net_id_;

    NetsInfo() {};
    ~NetsInfo() {};  
};

}

}
