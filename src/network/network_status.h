#pragma once

#include "common/utils.h"
#include "network/network_utils.h"

namespace shardora {

namespace network {

enum class Status : uint8_t {
    kClosed = 0,
    kPreopened = 1,
    kOpened = 2,
};


class NetInfo {
public:
    NetInfo() : status_(Status::kClosed), net_id_(common::kInvalidUint32) {}
    NetInfo(uint32_t net_id) {
        if (net_id == common::kInvalidUint32) {
            return;
        }
        
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id < network::kRootCongressNetworkId ||
            net_id >= network::kConsensusShardEndNetworkId) {
            return;
        }

        net_id_ = net_id;
        if (net_id < network::kInitOpenedShardCount + network::kConsensusShardBeginNetworkId) {
            status_ = Status::kOpened;
        } else {
            status_ = Status::kClosed;
        }
    }
    
    ~NetInfo() {};

    const inline Status Status() {
        return status_;
    }
    
    inline bool IsClosed() {
        return status_ == Status::kClosed;
    }
    
    inline bool IsPreopened() {
        return status_ == Status::kPreopened;
    }
    
    inline bool IsOpened() {
        return status_ == Status::kOpened;
    }
    
    void SetPreopened() {
        if (!IsClosed()) {
            return;
        }
        status_ = Status::kPreopened;
    }
    
    void SetOpened(uint32_t net_id) {
        if (!IsPreopened()) {
            return;
        }
        status_ = Status::kOpened;
    }    

private:
    enum Status status_;
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
            net_infos_[net_id] = NetInfo(net_id);
        }        
    }
    
private:
    NetInfo net_infos_[network::kConsensusShardEndNetworkId];

    NetsInfo() {};
    ~NetsInfo() {};  
};

}

}
