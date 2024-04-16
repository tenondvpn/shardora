#pragma once
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

// one for pool
class Consensus {
public:
    Consensus(const std::shared_ptr<ViewBlockChain> chain, const std::shared_ptr<Pacemaker> pacemaker, const std::shared_ptr<LeaderRotation> leader_rotation) :
        view_block_chain_(chain),
        pacemaker_(pacemaker),
        leader_rotation_(leader_rotation) {}
    ~Consensus() {};

    Consensus(const Consensus&) = delete;
    Consensus& operator=(const Consensus&) = delete;

    std::shared_ptr<Pacemaker> pacemaker() const {
        return pacemaker_;
    }

    std::shared_ptr<ViewBlockChain> chain() const {
        
    }
private:
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    std::shared_ptr<Pacemaker> pacemaker_;
    std::shared_ptr<LeaderRotation> leader_rotation_;
};

}

} // namespace shardora

