#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain_manager.h>

namespace shardora {
namespace consensus {

ViewBlockChainManager::ViewBlockChainManager() {
    // TODO Init 64 ViewBlockChains
    // tick_.CutOff(100000lu, std::bind(&ViewBlockChainManager::ConsumeOrphanBlocks, this));
}

ViewBlockChainManager::~ViewBlockChainManager() {}

// Status ViewBlockChainManager::Store(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& view_block) {
//     auto chain = Chain(pool_idx);
//     if (!chain) {
//         return Status::kError;
//     }

//     if (!view_block || !view_block->Valid()) {
//         return Status::kError;
//     }

//     // 同步逻辑放在调用层
//     // std::shared_ptr<ViewBlock> parent_block = nullptr;
//     // Status s = chain->Get(view_block->parent_hash, parent_block);
//     // if (s != Status::kSuccess || !parent_block) {
//     //     // 父块不存在，将 view_block 加入队列并同步父块
//     //     chain->AddOrphanBlock(view_block);
//     //     syncer_ptr_->AsyncFetch(view_block->parent_hash, pool_idx);
//     //     return Status::kError;
//     // }
    
//     return chain->Store(view_block);
// }

// Status ViewBlockChainManager::Get(const uint32_t& pool_idx, const HashStr& hash, std::shared_ptr<ViewBlock>& view_block) {
//     auto chain = Chain(pool_idx);
//     if (!chain) {
//         return Status::kError;
//     }

//     return chain->Get(hash, view_block);
// }


// bool ViewBlockChainManager::Extends(const uint32_t& pool_idx, const std::shared_ptr<ViewBlock>& block, const std::shared_ptr<ViewBlock>& target) {
//     auto chain = Chain(pool_idx);
//     if (!chain) {
//         return false;
//     }

//     return chain->Extends(block, target);
// }

// Status ViewBlockChainManager::PruneTo(const uint32_t &pool_idx, const HashStr& target, std::vector<std::shared_ptr<ViewBlock>> &forked_blockes) {
//     auto chain = Chain(pool_idx);
//     if (!chain) {
//         return Status::kError;
//     }

//     return chain->PruneTo(target, forked_blockes);
// }


// void ViewBlockChainManager::ConsumeOrphanBlocks() {
//     for (auto it = pool_chain_map_.begin(); it != pool_chain_map_.end(); it++) {
//         uint32_t pool_idx = it->first;
//         std::shared_ptr<ViewBlockChain> chain = it->second;
//         if (!chain) {
//             continue;
//         }

//         while (!chain->OrphanBlocks().empty()) {
//             std::shared_ptr<ViewBlock> orphan_block = chain->PopOrphanBlock();
//             // 超时的 view_block 不处理
//             if (chain->IsOrphanBlockTimeout(orphan_block)) {
//                 continue;
//             }
            
//             Store(pool_idx, orphan_block);
//         }
//     }
    
//     tick_.CutOff(100000lu, std::bind(&ViewBlockChainManager::ConsumeOrphanBlocks, this));    
// }
    
}
}
