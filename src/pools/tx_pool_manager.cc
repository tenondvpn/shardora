#include "pools/tx_pool_manager.h"

#include "common/log.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "network/network_utils.h"
#include "protos/prefix_db.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace pools {

TxPoolManager::TxPoolManager(std::shared_ptr<security::Security>& security) {
    security_ = security;
    tx_pool_ = new TxPool[common::kInvalidPoolIndex];
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        tx_pool_[i].Init(i);
    }
}

TxPoolManager::~TxPoolManager() {
    if (tx_pool_ != nullptr) {
        delete []tx_pool_;
    }
}

void TxPoolManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    // just one thread
    auto& header = msg_ptr->header;
    if (!header.has_tx_proto() || header.tx_proto().step() != pools::protobuf::kNormalFrom) {
        ZJC_DEBUG("not has tx proto or step error: %d", header.tx_proto().step());
        return;
    }

    auto& tx_msg = header.tx_proto();
    if (msg_ptr->address_info == nullptr) {
        ZJC_DEBUG("no address info.");
        return;
    }

    if (msg_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("sharding error: %d, %d",
            msg_ptr->address_info->sharding_id(),
            common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (tx_msg.has_key() && tx_msg.key().size() > kTxStorageKeyMaxSize) {
        ZJC_DEBUG("key size error now: %d, max: %d.",
            tx_msg.key().size(), kTxStorageKeyMaxSize);
        return;
    }

    msg_ptr->msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
    if (security_->Verify(
            msg_ptr->msg_hash,
            tx_msg.pubkey(),
            header.sign()) != security::kSecuritySuccess) {
        ZJC_ERROR("verify signature failed!");
        return;
    }

    // storage item not package in block, just package storage hash 
    SaveStorageToDb(header);
    msg_queues_[msg_ptr->address_info->pool_index()].push(msg_ptr);
}

void TxPoolManager::SaveStorageToDb(const transport::protobuf::Header& msg) {

}

void TxPoolManager::GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::vector<TxItemPtr>& res_vec) {
    if (count > common::kSingleBlockMaxTransactions) {
        count = common::kSingleBlockMaxTransactions;
    }

    while (msg_queues_[pool_index].size() > 0) {
        transport::MessagePtr msg_ptr = nullptr;
        msg_queues_[pool_index].pop(&msg_ptr);
        auto item_ptr = std::make_shared<TxItem>(msg_ptr);
        tx_pool_[pool_index].AddTx(item_ptr);
    }

    while (true) {
        auto tx = tx_pool_[pool_index].GetTx();
        if (tx == nullptr) {
            break;
        }

        res_vec.push_back(tx);
        if (res_vec.size() >= count) {
            break;
        }
    }
}

void TxPoolManager::GetTx(
        const common::BloomFilter& bloom_filter,
        uint32_t pool_index,
        std::vector<TxItemPtr>& res_vec) {
    while (msg_queues_[pool_index].size() > 0) {
        transport::MessagePtr msg_ptr = nullptr;
        msg_queues_[pool_index].pop(&msg_ptr);
        auto item_ptr = std::make_shared<TxItem>(msg_ptr);
        tx_pool_[pool_index].AddTx(item_ptr);
    }

    tx_pool_[pool_index].GetTx(bloom_filter, res_vec);
}

TxItemPtr TxPoolManager::GetTx(uint32_t pool_index, const std::string& sgid) {
    assert(pool_index < common::kInvalidPoolIndex);
    while (msg_queues_[pool_index].size() > 0) {
        transport::MessagePtr msg_ptr = nullptr;
        msg_queues_[pool_index].pop(&msg_ptr);
        auto item_ptr = std::make_shared<TxItem>(msg_ptr);
        tx_pool_[pool_index].AddTx(item_ptr);
    }

    return tx_pool_[pool_index].GetTx(sgid);
}

void TxPoolManager::TxRecover(uint32_t pool_index, std::vector<TxItemPtr>& recover_txs) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxRecover(recover_txs);
}

void TxPoolManager::TxOver(uint32_t pool_index, std::vector<TxItemPtr>& over_txs) {
    assert(pool_index < common::kInvalidPoolIndex);
    return tx_pool_[pool_index].TxOver(over_txs);
}

}  // namespace pools

}  // namespace zjchain
