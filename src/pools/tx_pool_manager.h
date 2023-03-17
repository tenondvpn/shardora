#pragma once

#include <bitset>
#include <memory>

#include "common/bitmap.h"
#include "common/thread_safe_queue.h"
#include "common/unique_map.h"
#include "pools/tx_pool.h"
#include "protos/address.pb.h"
#include "protos/pools.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace pools {

class TxPoolManager {
public:
    TxPoolManager(std::shared_ptr<security::Security>& security, std::shared_ptr<db::Db>& db);
    ~TxPoolManager();
    void HandleMessage(const transport::MessagePtr& msg);
    void GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map);
    void GetTx(
        const common::BloomFilter& bloom_filter,
        uint32_t pool_index,
        std::map<std::string, TxItemPtr>& res_map);
    inline TxItemPtr GetTx(uint32_t pool_index, const std::string& tx_hash) {
        assert(pool_index < common::kInvalidPoolIndex);
        //     while (msg_queues_[pool_index].size() > 0) {
        //         transport::MessagePtr msg_ptr = nullptr;
        //         msg_queues_[pool_index].pop(&msg_ptr);
        //         DispatchTx(pool_index, msg_ptr);
        //     }

        return tx_pool_[pool_index].GetTx(tx_hash);
    }

    std::shared_ptr<consensus::WaitingTxsItem> GetTx(
            uint32_t pool_index,
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list) {
        return tx_pool_[pool_index].GetTx(tx_hash_list);
    }

    void TxOver(uint32_t pool_index, std::map<std::string, TxItemPtr>& over_txs);
    void TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs);
    void SetTimeout(uint32_t pool_index) {}
    void RegisterCreateTxFunction(uint32_t type, CreateConsensusItemFunction func) {
        assert(type < pools::protobuf::StepType_ARRAYSIZE);
        item_functions_[type] = func;
    }

    uint64_t latest_height(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_height();
    }

    std::string latest_hash(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_hash();
    }

    // just for test
    int AddTx(uint32_t pool_index, TxItemPtr& tx_ptr) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return kPoolsError;
        }

        return tx_pool_[pool_index].AddTx(tx_ptr);
    }

    void UpdateLatestInfo(
            uint32_t pool_index,
            uint64_t height,
            const std::string& hash,
            db::DbWriteBach& db_batch) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return;
        }

        pools::protobuf::PoolLatestInfo pool_info;
        pool_info.set_height(height);
        pool_info.set_hash(hash);
        prefix_db_->SaveLatestPoolInfo(pool_index, pool_info, db_batch);
        return tx_pool_[pool_index].UpdateLatestInfo(height, hash);
    }

private:
    void InitAllPoolInfo() {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            pools::protobuf::PoolLatestInfo pool_info;
            if (!prefix_db_->GetLatestPoolInfo(i, &pool_info)) {
                continue;
            }

            tx_pool_[i].UpdateLatestInfo(pool_info.height(), pool_info.hash());
        }
    }
    void SaveStorageToDb(const transport::protobuf::Header& msg);
    void DispatchTx(uint32_t pool_index, transport::MessagePtr& msg_ptr);
    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr);

    TxPool* tx_pool_{ nullptr };
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
//     common::ThreadSafeQueue<transport::MessagePtr> msg_queues_[common::kInvalidPoolIndex];
    CreateConsensusItemFunction item_functions_[pools::protobuf::StepType_ARRAYSIZE] = { nullptr };
    common::UniqueMap<std::string, protos::AddressInfoPtr> address_map_;

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace zjchain
