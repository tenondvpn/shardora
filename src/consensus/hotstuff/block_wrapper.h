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
    virtual Status Return(const std::shared_ptr<block::protobuf::Block>&) = 0;
};

class BlockWrapper : public IBlockWrapper {
public:
    explicit BlockWrapper(const std::shared_ptr<IBlockAcceptor>& acceptor);;
    ~BlockWrapper();

    BlockWrapper(const BlockWrapper&) = delete;
    BlockWrapper& operator=(const BlockWrapper&) = delete;

    Status Wrap(std::shared_ptr<block::protobuf::Block>& block) override;
    Status Return(const std::shared_ptr<block::protobuf::Block>& block) override;

private:
    std::shared_ptr<IBlockAcceptor> acceptor_;
};

} // namespace hotstuff

} // namespace shardora

