#pragma once

#include "common/log.h"
#include "common/utils.h"

namespace zjchain {

namespace elect {

class ElectNodesStoke {
public:
    ElectNodesStoke();
    ~ElectNodesStoke();
    void NewBlockWithTx(
            uint8_t thread_idx,
            const std::shared_ptr<block::protobuf::Block>& block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {

    }

private:

    DISALLOW_COPY_AND_ASSIGN(ElectNodesStoke);
};

}  // namespace elect

}  // namespace zjchain
