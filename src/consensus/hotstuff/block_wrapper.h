#pragma once

#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

class IBlockWrapper {
public:
    IBlockWrapper() = default;
    virtual ~IBlockWrapper() {};

    virtual Status Wrap(std::shared_ptr<block::protobuf::Block>&) = 0;
};

class BlockWrapper : public IBlockWrapper {
public:
    BlockWrapper(
            const uint32_t pool_idx,
            const std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    ~BlockWrapper();

    BlockWrapper(const BlockWrapper&) = delete;
    BlockWrapper& operator=(const BlockWrapper&) = delete;

    Status Wrap(std::shared_ptr<block::protobuf::Block>& block) override;

private:
    uint32_t pool_idx_;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
};

} // namespace hotstuff

} // namespace shardora

