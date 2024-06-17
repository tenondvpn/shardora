#pragma once

#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

// 用于计算和提交 leader 的共识数据
class ConsensusStatAcceptor {
public:
    ConsensusStatAcceptor(
            uint32_t pool_idx,
            std::shared_ptr<ElectInfo>& elect_info,
            std::shared_ptr<ViewBlockChain>& view_block_chain);
    ~ConsensusStatAcceptor();

    ConsensusStatAcceptor(const ConsensusStatAcceptor&) = delete;
    ConsensusStatAcceptor& operator=(const ConsensusStatAcceptor&) = delete;

    // 接收并计算 leader 的共识统计数据
    Status Accept(std::shared_ptr<ViewBlock>& v_block);
    // 提交并生效共识统计数据
    Status Commit(const std::shared_ptr<ViewBlock>& v_block);

private:
    uint32_t pool_idx_;
    std::unordered_map<uint32_t, uint64_t> leader_last_commit_height_map_; // member_index => View, 记录所有 leader 最后一次提交的 View
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<ViewBlockChain> view_block_chain_;
};

} // namespace hotstuff

} // namespace shardora

