#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <set>
#include <deque>
#include <queue>

#include "common/bloom_filter.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/time_utils.h"
#include "common/user_property_key_define.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "pools/tx_utils.h"
#include "protos/pools.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace pools {

class TxItem {
public:
    virtual ~TxItem() {}

    TxItem(transport::MessagePtr& msg) : msg_ptr(msg) {
        time_valid = common::TimeUtils::TimestampUs() + kBftStartDeltaTime;
#ifdef ZJC_UNITTEST
        time_valid = 0;
#endif // ZJC_UNITTEST
        timeout = common::TimeUtils::TimestampUs() + kTxPoolTimeoutUs;
        remove_timeout = timeout + kTxPoolTimeoutUs;
        gas_price = msg->header.tx_proto().gas_price();
        if (msg->header.tx_proto().has_step()) {
            step = msg->header.tx_proto().step();
        }

        tx_hash = common::Hash::keccak256(
            msg->header.tx_proto().gid() + std::to_string(step) + msg->msg_hash);
    }
    
    virtual int HandleTx(
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) = 0;
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) = 0;

    void DefaultTxItem(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        block_tx->set_gid(tx_info.gid());
        block_tx->set_from_pubkey(tx_info.pubkey());
        block_tx->set_gas_limit(tx_info.gas_limit());
        block_tx->set_gas_price(tx_info.gas_price());
        block_tx->set_step(tx_info.step());
        block_tx->set_from_pubkey(tx_info.pubkey());
        block_tx->set_to(tx_info.to());
        block_tx->set_amount(tx_info.amount());
    }

    transport::MessagePtr msg_ptr;
    uint64_t timeout;
    uint64_t remove_timeout;
    uint64_t time_valid{ 0 };
    uint64_t gas_price{ 0 };
    int32_t step = pools::protobuf::kNormalFrom;
    std::string from_addr;
    std::string tx_hash;
};

class FromTxItem : public TxItem {
public:
    FromTxItem(transport::MessagePtr& msg) : TxItem(msg) {}

    virtual int HandleTx(
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t gas_used = 0;
        // gas just consume by from
        uint64_t from_balance = 0;
        uint64_t to_balance = 0;
        auto& from = msg_ptr->address_info->addr();
        int balance_status = GetTempAccountBalance(from, acc_balance_map, &from_balance);
        if (balance_status != kBftSuccess) {
            block_tx.set_status(balance_status);
            // will never happen
            assert(false);
            return kBftError;
        }

        do  {
            gas_used = kTransferGas;
            for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                // TODO(): check key exists and reserve gas
                gas_used += (block_tx.storages(i).key().size() + block_tx.storages(i).val_size()) *
                    kKeyValueStorageEachBytes;
            }

            if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
                block_tx.set_status(kBftUserSetGasLimitError);
                ZJC_DEBUG("balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), block_tx.gas_price());
                break;
            }

            if (block_tx.gas_limit() < gas_used) {
                block_tx.set_status(kBftUserSetGasLimitError);
                ZJC_DEBUG("1 balance error: %lu, %lu, %lu", from_balance, block_tx.gas_limit(), gas_used);
                break;
            }
        } while (0);

        if (block_tx.status() == kBftSuccess) {
            uint64_t dec_amount = block_tx.amount() + gas_used * block_tx.gas_price();
            if (from_balance >= gas_used * block_tx.gas_price()) {
                if (from_balance >= dec_amount) {
                    from_balance -= dec_amount;
                } else {
                    from_balance -= gas_used * block_tx.gas_price();
                    block_tx.set_status(kBftAccountBalanceError);
                    ZJC_ERROR("leader balance error: %llu, %llu", from_balance, dec_amount);
                }
            } else {
                from_balance = 0;
                block_tx.set_status(kBftAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu",
                    from_balance, gas_used * block_tx.gas_price());
            }
        } else {
            if (from_balance >= gas_used * block_tx.gas_price()) {
                    from_balance -= gas_used * block_tx.gas_price();
            } else {
                from_balance = 0;
            }
        }

        acc_balance_map[from] = from_balance;
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
        ZJC_DEBUG("handle tx success: %s, %lu, %lu, status: %d",
            common::Encode::HexEncode(block_tx.gid()).c_str(),
            block_tx.balance(),
            block_tx.gas_used(),
            block_tx.status());
        return kConsensusSuccess;
    }

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        DefaultTxItem(tx_info, block_tx);
        // change
        if (!tx_info.key().empty()) {
            auto storage = block_tx->add_storages();
            storage->set_key(tx_info.key());
            if (tx_info.value().size() <= 32) {
                storage->set_val_hash(tx_info.value());
            } else {
                storage->set_val_hash(common::Hash::keccak256(tx_info.value()));
            }

            storage->set_val_size(tx_info.value().size());
        }

        return kConsensusSuccess;
    }
};

class ToTxItem : public TxItem {
public:
    ToTxItem(transport::MessagePtr& msg) : TxItem(msg) {}

    virtual int HandleTx(
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        return kConsensusSuccess;
    }

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        DefaultTxItem(tx_info, block_tx);
        // change
        if (tx_info.key().empty()) {
            return kConsensusError;
        }
            
        auto storage = block_tx->add_storages();
        storage->set_key(tx_info.key());
        storage->set_val_hash(tx_info.value());
        storage->set_val_size(0);
        return kConsensusSuccess;
    }
};

class ContractItem : public TxItem {
public:
    ContractItem(transport::MessagePtr& msg) : TxItem(msg) {}

    virtual int HandleTx(
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        return kConsensusSuccess;
    }

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        return kConsensusSuccess;
    }
};

typedef std::shared_ptr<TxItem> TxItemPtr;

struct TxItemPriOper {
    bool operator() (TxItemPtr& a, TxItemPtr& b) {
        return a->gas_price < b->gas_price;
    }
};

class TxPool {
public:
    TxPool();
    ~TxPool();
    void Init(uint32_t pool_idx);
    int AddTx(TxItemPtr& tx_ptr);
//     bool IsPrevTxsOver() {
//         return waiting_txs_.empty();
//     }

    TxItemPtr GetTx();
    TxItemPtr GetTx(const std::string& sgid);
    void GetTx(
        const common::BloomFilter& bloom_filter,
        std::map<std::string, TxItemPtr>& res_map);
    void TxOver(std::map<std::string, TxItemPtr>& txs);
    void TxRecover(std::map<std::string, TxItemPtr>& txs);
    uint64_t latest_height() const {
        return latest_height_;
    }

    std::string latest_hash() const {
        return latest_hash_;
    }

private:
    bool CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now);

    std::priority_queue<TxItemPtr, std::vector<TxItemPtr>, TxItemPriOper> mem_queue_;
//     std::set<std::string> waiting_txs_;
    std::deque<TxItemPtr> timeout_txs_;
    std::unordered_map<std::string, TxItemPtr> added_tx_map_;
    uint32_t pool_index_ = 0;
    uint64_t latest_height_ = 0;
    std::string latest_hash_;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace zjchain
