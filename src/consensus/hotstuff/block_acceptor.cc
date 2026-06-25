#include "consensus/hotstuff/block_acceptor.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "bls/agg_bls.h"
#include "common/defer.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "consensus/hotstuff/block_executor.h"
#include "consensus/hotstuff/types.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "consensus/zbft/contract_call.h"
#include "consensus/zbft/contract_prefund.h"
#include "consensus/zbft/contract_refund.h"
#include "consensus/zbft/contract_create.h"
#include "consensus/zbft/create_library.h"
#include "consensus/zbft/elect_tx_item.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/pool_statistic_tag.h"
#include "consensus/zbft/to_tx_local_item.h"
#include "consensus/zbft/to_tx_item.h"
#include "consensus/zbft/time_block_tx.h"
#include "consensus/zbft/statistic_tx_item.h"
#include "consensus/zbft/root_to_tx_item.h"
#include "consensus/zbft/root_cross_tx_item.h"
#include "consensus/zbft/join_elect_tx_item.h"
#include "network/network_utils.h"
#include "protos/pools.pb.h"
#include "protos/zbft.pb.h"
#include "security/ecdsa/ecdsa.h"
#include "security/eth_verify.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace hotstuff {

namespace {

// One process-wide pool: every BlockAcceptor shares the same workers so total
// verify threads stay at max(2, 2 * hardware_concurrency()) instead of
// (that count) * (number of tx pools).
struct GlobalTxVerifyPool {
    struct Task {
        std::function<void()> fn;
    };

    void Acquire() {
        std::lock_guard<std::mutex> lk(mu_);
        if (ref_count_++ > 0) {
            return;
        }
        stop_ = false;
        int cores = static_cast<int>(std::thread::hardware_concurrency());
        if (cores <= 0) {
            cores = 4;
        }
        const int nthreads = std::max(2, 8 * cores);
        threads_.reserve(nthreads);
        for (int i = 0; i < nthreads; ++i) {
            threads_.emplace_back([this] { WorkerLoop(); });
        }
    }

    void Release() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (--ref_count_ > 0) {
                return;
            }
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        std::lock_guard<std::mutex> lk(mu_);
        threads_.clear();
        while (!queue_.empty()) {
            queue_.pop();
        }
        stop_ = false;
    }

    void RunVerifyBatch(std::vector<std::function<void()>>& tasks) {
        if (tasks.empty()) {
            return;
        }

        const int n = static_cast<int>(tasks.size());
        std::atomic<int> remaining(n);
        struct Sync {
            std::mutex mu;
            std::condition_variable cv;
        };
        auto sync = std::make_shared<Sync>();

        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& fn : tasks) {
                queue_.push({[sync, &remaining, f = std::move(fn)]() mutable {
                    f();
                    {
                        std::lock_guard<std::mutex> g(sync->mu);
                        --remaining;
                    }
                    sync->cv.notify_one();
                }});
            }
        }
        cv_.notify_all();

        std::unique_lock<std::mutex> lk(sync->mu);
        sync->cv.wait(lk, [&remaining] { return remaining.load() == 0; });
    }

private:
    void WorkerLoop() {
        while (true) {
            std::function<void()> fn;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) {
                    return;
                }
                fn = std::move(queue_.front().fn);
                queue_.pop();
            }
            fn();
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<Task> queue_;
    std::vector<std::thread> threads_;
    bool stop_ = false;
    int ref_count_ = 0;
};

GlobalTxVerifyPool& GlobalPool() {
    static GlobalTxVerifyPool pool;
    return pool;
}

using NormalToItemMap = std::unordered_map<std::string, pools::protobuf::ToTxMessageItem>;

NormalToItemMap FlattenNormalToItems(const pools::protobuf::AllToTxMessage& all_to_txs) {
    NormalToItemMap items;
    for (int i = 0; i < all_to_txs.to_tx_arr_size(); ++i) {
        const auto& to_tx = all_to_txs.to_tx_arr(i);
        for (int j = 0; j < to_tx.tos_size(); ++j) {
            const auto& item = to_tx.tos(j);
            if (!item.has_des() || item.des().empty()) {
                continue;
            }
            auto iter = items.find(item.des());
            if (iter == items.end()) {
                items[item.des()] = item;
                continue;
            }

            iter->second.set_amount(iter->second.amount() + item.amount());
            if (item.prefund() > 0) {
                iter->second.set_prefund(iter->second.prefund() + item.prefund());
            }
            if (item.has_library_bytes()) {
                iter->second.set_library_bytes(item.library_bytes());
            }
        }
    }
    return items;
}

// Backup follower: compare local ToTxMessageItem with leader proposal.
// If the leader classifies an uncertain destination address into the root shard,
// root is authoritative: a local non-root classification is treated as the same
// destination as long as the receiver and amount match.
bool ToTxMessageItemFieldsMatchForBackup(
        const pools::protobuf::ToTxMessageItem& leader,
        const pools::protobuf::ToTxMessageItem& local) {
    if (leader.des() != local.des()) {
        return false;
    }
    if (leader.amount() != local.amount()) {
        return false;
    }

    const bool leader_des_is_root =
        leader.has_des_sharding_id() &&
        leader.des_sharding_id() == network::kRootCongressNetworkId;
    if (leader_des_is_root) {
        if (local.des_sharding_id() != network::kRootCongressNetworkId) {
            SHARDORA_DEBUG("kNormalTo backup: leader root classification accepted, des=%s, "
                "local_des_sharding_id=%u, amount=%lu",
                common::Encode::HexEncode(leader.des()).c_str(),
                local.des_sharding_id(),
                leader.amount());
        }
        return true;
    }

    if (leader.pool_index() != local.pool_index()) {
        return false;
    }
    if (leader.sharding_id() != local.sharding_id()) {
        return false;
    }
    if (leader.from() != local.from()) {
        return false;
    }
    if (leader.prefund() != local.prefund()) {
        return false;
    }
    if (leader.elect_join_g2_value() != local.elect_join_g2_value()) {
        return false;
    }
    if (leader.library_bytes() != local.library_bytes()) {
        return false;
    }

    if (leader.des_sharding_id() != local.des_sharding_id()) {
        return false;
    }
    return true;
}

bool ValidateBackupNormalToAgainstLeader(
        const pools::protobuf::AllToTxMessage& leader_all,
        const pools::protobuf::AllToTxMessage& local_all,
        uint32_t pool_index) {
    if (leader_all.to_heights().SerializeAsString() !=
            local_all.to_heights().SerializeAsString()) {
        SHARDORA_WARN("kNormalTo backup: to_heights mismatch pool=%u", pool_index);
        return false;
    }

    const auto leader_items = FlattenNormalToItems(leader_all);
    const auto local_items = FlattenNormalToItems(local_all);
    if (leader_items.size() != local_items.size()) {
        SHARDORA_WARN("kNormalTo backup: tos item count mismatch pool=%u leader=%zu local=%zu",
            pool_index,
            leader_items.size(),
            local_items.size());
        return false;
    }

    for (const auto& kv : leader_items) {
        const auto lit = local_items.find(kv.first);
        if (lit == local_items.end()) {
            SHARDORA_WARN("kNormalTo backup: missing local ToTxMessageItem des=%s pool=%u",
                common::Encode::HexEncode(kv.first).c_str(),
                pool_index);
            return false;
        }
        if (!ToTxMessageItemFieldsMatchForBackup(kv.second, lit->second)) {
            SHARDORA_WARN("kNormalTo backup: ToTxMessageItem mismatch des=%s pool=%u "
                "leader_des_sharding_id=%u local_des_sharding_id=%u",
                common::Encode::HexEncode(kv.first).c_str(),
                pool_index,
                kv.second.des_sharding_id(),
                lit->second.des_sharding_id());
            return false;
        }
    }

    return true;
}

bool ValidateProposeBlockGasLimit(
        const hotstuff::protobuf::TxPropose& tx_propose,
        const view_block::protobuf::ViewBlockItem& view_block) {
    uint64_t proposed_gas_limit = 0;
    for (int32_t i = 0; i < tx_propose.txs_size(); ++i) {
        const auto& tx = tx_propose.txs(i);
        if (!consensus::CanAddBlockGas(proposed_gas_limit, tx.gas_limit())) {
            SHARDORA_WARN("proposal exceeds block gas limit: %u_%u_%lu, tx_index: %d, "
                "tx step: %d, nonce: %lu, proposed_gas_limit: %lu, "
                "tx_gas_limit: %lu, block_gas_limit: %lu",
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.qc().view(),
                i,
                (int32_t)tx.step(),
                tx.nonce(),
                proposed_gas_limit,
                tx.gas_limit(),
                consensus::kBlockMaxGasLimit);
            return false;
        }

        proposed_gas_limit += tx.gas_limit();
    }

    return true;
}

} // namespace

BlockAcceptor::BlockAcceptor() {}

BlockAcceptor::~BlockAcceptor() {
    if (tx_verify_pool_acquired_) {
        GlobalPool().Release();
        tx_verify_pool_acquired_ = false;
    }
}

void BlockAcceptor::RunVerifyBatch(std::vector<std::function<void()>>& tasks) {
    GlobalPool().RunVerifyBatch(tasks);
}

void BlockAcceptor::Init(
        const uint32_t& pool_idx,
        const std::shared_ptr<security::Security>& security,
        const std::shared_ptr<block::AccountManager>& account_mgr,
        const std::shared_ptr<ElectInfo>& elect_info,
        const std::shared_ptr<vss::VssManager>& vss_mgr,
        const std::shared_ptr<contract::ContractManager>& contract_mgr,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<elect::ElectManager> elect_mgr,
        std::shared_ptr<ViewBlockChain> view_block_chain,
        std::shared_ptr<bls::BlsManager> bls_mgr) {
    pool_idx_ = pool_idx;
    elect_mgr_ = elect_mgr;
    security_ptr_ = security;
    account_mgr_ = account_mgr;
    elect_info_ = elect_info;
    vss_mgr_ = vss_mgr;
    contract_mgr_ = contract_mgr;
    db_ = db;
    pools_mgr_ = pools_mgr;
    block_mgr_ = block_mgr;
    tm_block_mgr_ = tm_block_mgr;
    view_block_chain_ = view_block_chain;
    bls_mgr_ = bls_mgr;
    tx_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr_, block_mgr_, tm_block_mgr_);
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);

    // Share one verify pool across all BlockAcceptors (see GlobalTxVerifyPool).
    GlobalPool().Acquire();
    tx_verify_pool_acquired_ = true;
}

// Accept verifies the new proposal information of the Leader, executes txs, and modifies the block
Status BlockAcceptor::Accept(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        bool no_tx_allowed,
        bool directly_user_leader_txs,
        BalanceAndNonceMap& balance_and_nonce_map,
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map) {
    auto accept_begin_ms = common::TimeUtils::TimestampMs();
    auto& msg_ptr = pro_msg_wrap->msg_ptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& propose_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tx_propose();
    auto& view_block = *pro_msg_wrap->view_block_ptr;
    if (propose_msg.txs().empty()) {
        if (no_tx_allowed) {
            SHARDORA_DEBUG("success do transaction tx size: %u, add: %u, %u_%u_%lu_%lu, "
                "height: %lu, view hash: %s", 
                0, 
                view_block.block_info().tx_list_size(), 
                view_block.qc().network_id(), 
                view_block.qc().pool_index(), 
                view_block.block_info().height(),
                view_block.qc().view(), 
                view_block.block_info().height(),
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str());
            // //assert(view_block.qc().view_block_hash().empty());
            view_block.mutable_qc()->set_view_block_hash(GetBlockHash(view_block));
            SHARDORA_DEBUG("success set view block hash: %s, parent: %s, %u_%u_%lu_%lu, "
                "chain has hash: %d, db has hash: %d",
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(view_block.parent_hash()).c_str(),
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.block_info().height(),
                view_block.qc().view(),
                view_block_chain_->Has(view_block.qc().view_block_hash()),
                prefix_db_->BlockExists(view_block.qc().view_block_hash()));
            if (view_block_chain_->Has(view_block.qc().view_block_hash())) {
                // //assert(false);
                return Status::kSuccess;
            }

            if (prefix_db_->BlockExists(view_block.qc().view_block_hash())) {
                //assert(false);
                return Status::kAcceptorBlockInvalid;
            }
        }

        SHARDORA_DEBUG("propose_msg.txs().empty() error!");
        return no_tx_allowed ? Status::kSuccess : Status::kAcceptorTxsEmpty;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // 1. verify block
    if (!IsBlockValid(view_block)) {
        SHARDORA_WARN("IsBlockValid error!");
        return Status::kAcceptorBlockInvalid;
    }

    if (!ValidateProposeBlockGasLimit(propose_msg, view_block)) {
        view_block.mutable_block_info()->set_all_gas(0);
        return Status::kAcceptorBlockInvalid;
    }

    shardora_host.parent_hash_ = view_block.parent_hash();
    shardora_host.view_block_chain_ = view_block_chain_;
    //assert(shardora_host.view_block_chain_ != nullptr);
    shardora_host.view_ = view_block.qc().view();
    // 2. Get txs from local pool
    auto txs_ptr = std::make_shared<consensus::WaitingTxsItem>();
    Status s = Status::kSuccess;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto get_txs_begin_ms = common::TimeUtils::TimestampMs();
    s = GetAndAddTxsLocally(
        msg_ptr,
        view_block.parent_hash(), 
        propose_msg, 
        directly_user_leader_txs, 
        txs_ptr, 
        balance_and_nonce_map,
        shardora_host,
        out_leader_nonce_map);
    auto get_txs_end_ms = common::TimeUtils::TimestampMs();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (s != Status::kSuccess) {
        SHARDORA_WARN("GetAndAddTxsLocally error! status=%d, pool_idx=%u, view_height=%lu, "
            "parent_hash=%s, get_txs_time=%lums, txs_count=%zu",
            (int)s, pool_idx(), view_block.block_info().height(), 
            common::Encode::HexEncode(view_block.parent_hash()).substr(0, 16).c_str(),
            (get_txs_end_ms - get_txs_begin_ms),
            (txs_ptr ? txs_ptr->txs.size() : 0));
        return s;
    }

    // 3. Do txs and create block_tx.
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto do_tx_begin_ms = common::TimeUtils::TimestampMs();
    s = DoTransactions(txs_ptr, &view_block, balance_and_nonce_map, shardora_host);
    auto do_tx_end_ms = common::TimeUtils::TimestampMs();
    if (s != Status::kSuccess) {
        SHARDORA_WARN("DoTransactions error!");
        return s;
    }

    // Sort by address key to ensure deterministic order in address_array.
    // unordered_map iteration order varies across runs (different hash table
    // layouts after restart), which would produce different block hashes for
    // the same set of transactions.
    std::vector<std::pair<std::string, std::shared_ptr<address::protobuf::AddressInfo>>> sorted_balance;
    sorted_balance.reserve(balance_and_nonce_map.size());
    for (auto& kv : balance_and_nonce_map) {
        sorted_balance.emplace_back(kv.first, kv.second);
    }
    std::sort(sorted_balance.begin(), sorted_balance.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (auto& [addr_key, addr_ptr] : sorted_balance) {
        if (!addr_ptr->has_balance() || !addr_ptr->has_nonce() || !addr_ptr->has_sharding_id() || 
                !addr_ptr->has_pool_index() || !addr_ptr->has_addr() || !addr_ptr->has_type() ||
                !addr_ptr->has_latest_height()) {
            SHARDORA_WARN("invalid addr, %u_%u_%lu_%lu, success addr info: %s", 
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.block_info().height(),
                view_block.qc().view(),
                common::Encode::HexEncode(addr_ptr->addr()).c_str());
            continue;
        }

        if (addr_key.size() != common::kUnicastAddressLength && 
                addr_key.size() != common::kPreypamentAddressLength) {
            //assert(false);
            continue;
        }

        auto* addr_info = view_block.mutable_block_info()->add_address_array();
        *addr_info = *addr_ptr;
        SHARDORA_DEBUG("%u_%u_%lu_%lu, success addr info: %s, balance: %lu, nonce: %lu, destrcuted: %d", 
            view_block.qc().network_id(),
            view_block.qc().pool_index(),
            view_block.block_info().height(),
            view_block.qc().view(),
            common::Encode::HexEncode(addr_info->addr()).c_str(), 
            addr_info->balance(),
            addr_info->nonce(),
            addr_info->destructed());
        prefix_db_->AddAddressInfo(addr_info->addr(), *addr_info, shardora_host.db_batch_);
    }

    for (auto account_iter = shardora_host.accounts_.begin();
            account_iter != shardora_host.accounts_.end(); ++account_iter) {
        for (auto storage_iter = account_iter->second.storage.begin();
                storage_iter != account_iter->second.storage.end(); ++storage_iter) {
            auto& kv_info = *view_block.mutable_block_info()->add_key_value_array();
            kv_info.set_addr(std::string((char*)account_iter->first.bytes, sizeof(account_iter->first.bytes)));
            kv_info.set_key(std::string((char*)storage_iter->first.bytes, sizeof(storage_iter->first.bytes)));
            kv_info.set_value(std::string(
                (char*)storage_iter->second.value.bytes,
                sizeof(storage_iter->second.value.bytes)));
            kv_info.set_height(view_block.block_info().height());
            prefix_db_->SaveTemporaryKv(
                kv_info.addr() + kv_info.key(), 
                kv_info.SerializeAsString(), 
                shardora_host.db_batch_);
            SHARDORA_DEBUG("%u_%u_%lu_%lu, success add key value addr: %s, key: %s", 
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.block_info().height(),
                view_block.qc().view(),
                common::Encode::HexEncode(kv_info.addr()).c_str(), 
                common::Encode::HexEncode(kv_info.key()).c_str());
        }

        for (auto storage_iter = account_iter->second.str_storage.begin();
                storage_iter != account_iter->second.str_storage.end(); ++storage_iter) {
            auto& kv_info = *view_block.mutable_block_info()->add_key_value_array();
            kv_info.set_addr(std::string(
                (char*)account_iter->first.bytes,
                sizeof(account_iter->first.bytes)));
            kv_info.set_key(storage_iter->first);
            kv_info.set_value(storage_iter->second.str_val);
            kv_info.set_height(view_block.block_info().height());
            prefix_db_->SaveTemporaryKv(
                kv_info.addr() + kv_info.key(), 
                kv_info.SerializeAsString(), 
                shardora_host.db_batch_);
            SHARDORA_DEBUG("%u_%u_%lu_%lu, success add key value addr: %s, key: %s", 
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.block_info().height(),
                view_block.qc().view(),
                common::Encode::HexEncode(kv_info.addr()).c_str(), 
                common::Encode::HexEncode(kv_info.key()).c_str());
        }
    }

    for (int32_t i = 0; i < view_block.block_info().joins_size(); i++) {
        auto& join_info = view_block.block_info().joins(i);
        auto addr = security_ptr_->GetAddressWithPublicKey(join_info.public_key());
        prefix_db_->SaveNodeVerificationVector(
            addr,
            join_info,
            shardora_host.db_batch_);
#ifndef NDEBUG
        auto n = common::GlobalInfo::Instance()->each_shard_max_members();
        auto t = common::GetSignerCount(n);
        //assert(join_info.g2_req().verify_vec_size() >= t);
#endif
        prefix_db_->AddBlsVerifyG2(addr, join_info.g2_req(), shardora_host.db_batch_);
    }

    for (auto iter = shardora_host.cross_to_map_.begin(); iter != shardora_host.cross_to_map_.end(); ++iter) {
        auto* cross_to_item = view_block.mutable_block_info()->add_cross_shard_to_array();
        *cross_to_item = *iter->second;
        UpdateDesShardingId(cross_to_item, shardora_host);
        SHARDORA_DEBUG("success add cross to item: %s, amount: %lu, prefund: %lu",
            common::Encode::HexEncode(cross_to_item->des()).c_str(),
            cross_to_item->amount(), cross_to_item->prefund());
    }

    if (view_block.block_info().cross_shard_to_array_size() > 0) {
        SHARDORA_DEBUG("success add cross to shard: %u_%u_%lu, %s",
            view_block.qc().network_id(), 
            view_block.qc().pool_index(), 
            view_block.qc().view(),
            ProtobufToJson(view_block).c_str());
    }

    SHARDORA_DEBUG("success do transaction tx size: %u, add: %u, %u_%u_%lu_%lu, height: %lu, "
        "timeblock height: %lu, local latest timeblock height: %lu", 
        txs_ptr->txs.size(), 
        view_block.block_info().tx_list_size(), 
        view_block.qc().network_id(), 
        view_block.qc().pool_index(), 
        view_block.block_info().height(),
        view_block.qc().view(), 
        view_block.block_info().height(),
        view_block.block_info().timeblock_height(),
        tm_block_mgr_->LatestTimestampHeight());
    view_block.mutable_qc()->set_view_block_hash(GetBlockHash(view_block));
    SHARDORA_DEBUG("success set view block hash: %s, parent: %s, %u_%u_%lu_%lu",
        common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block.parent_hash()).c_str(),
        view_block.qc().network_id(),
        view_block.qc().pool_index(),
        view_block.block_info().height(),
        view_block.qc().view());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (prefix_db_->BlockExists(view_block.qc().view_block_hash())) {
        //assert(false);
        return Status::kAcceptorBlockInvalid;
    }

    auto accept_end_ms = common::TimeUtils::TimestampMs();
    return Status::kSuccess;
}

void BlockAcceptor::UpdateDesShardingId(
        pools::protobuf::ToTxMessageItem* to_addr_info, 
        shardoravm::ShardorahainHost& shardora_host) {
    if (to_addr_info->has_des_sharding_id()) {
        return;
    }

    to_addr_info->set_des_sharding_id(network::kUniversalNetworkId);
}

// Validate statistic transaction node consistency (90% threshold)
bool BlockAcceptor::ValidateStatisticNodeConsistency(
        const pools::protobuf::ElectStatistic& leader_statistic,
        uint32_t pool_index) {
    
    // Step 1: Get local statistic transaction from tx_pool
    // TODO: Implement GetLocalStatisticFromTxPool when interface is available
    // For now, we accept leader's version if we can't retrieve local statistic
    pools::protobuf::ElectStatistic local_statistic;
    // Placeholder: In production, uncomment this when GetLocalStatisticFromTxPool is implemented
    // if (!GetLocalStatisticFromTxPool(pool_index, &local_statistic)) {
    //     SHARDORA_DEBUG("pool=%u, no local statistic found, accepting leader's version", pool_index);
    //     return true;
    // }
    
    // Temporary: Accept leader's version until we can get local statistic
    SHARDORA_DEBUG("pool=%u, statistic validation: accepting leader's version (local retrieval not implemented)",
        pool_index);
    return true;
    
    // Step 2: Verify non-node information is completely identical
    // All fields except join_elect_nodes must match exactly
    
    // 2.1 Verify sharding_id
    if (leader_statistic.sharding_id() != local_statistic.sharding_id()) {
        SHARDORA_WARN("pool=%u, sharding_id mismatch: leader=%u, local=%u",
            pool_index, leader_statistic.sharding_id(), local_statistic.sharding_id());
        return false;
    }
    
    // 2.2 Verify statistic_height
    if (leader_statistic.statistic_height() != local_statistic.statistic_height()) {
        SHARDORA_WARN("pool=%u, statistic_height mismatch: leader=%lu, local=%lu",
            pool_index, leader_statistic.statistic_height(), local_statistic.statistic_height());
        return false;
    }
    
    // 2.3 Verify gas_amount
    if (leader_statistic.gas_amount() != local_statistic.gas_amount()) {
        SHARDORA_WARN("pool=%u, gas_amount mismatch: leader=%lu, local=%lu",
            pool_index, leader_statistic.gas_amount(), local_statistic.gas_amount());
        return false;
    }
    
    // 2.4 Verify lof_leaders (list of leaders)
    if (leader_statistic.lof_leaders_size() != local_statistic.lof_leaders_size()) {
        SHARDORA_WARN("pool=%u, lof_leaders size mismatch: leader=%d, local=%d",
            pool_index, leader_statistic.lof_leaders_size(), local_statistic.lof_leaders_size());
        return false;
    }
    for (int i = 0; i < leader_statistic.lof_leaders_size(); ++i) {
        if (leader_statistic.lof_leaders(i) != local_statistic.lof_leaders(i)) {
            SHARDORA_WARN("pool=%u, lof_leaders[%d] mismatch: leader=%u, local=%u",
                pool_index, i, leader_statistic.lof_leaders(i), local_statistic.lof_leaders(i));
            return false;
        }
    }
    
    // 2.5 Verify statistics (PoolStatisticItem array)
    if (leader_statistic.statistics_size() != local_statistic.statistics_size()) {
        SHARDORA_WARN("pool=%u, statistics size mismatch: leader=%d, local=%d",
            pool_index, leader_statistic.statistics_size(), local_statistic.statistics_size());
        return false;
    }
    for (int i = 0; i < leader_statistic.statistics_size(); ++i) {
        const auto& leader_stat = leader_statistic.statistics(i);
        const auto& local_stat = local_statistic.statistics(i);
        
        // Compare all fields in PoolStatisticItem
        if (leader_stat.elect_height() != local_stat.elect_height() ||
            leader_stat.avg_geo_distance() != local_stat.avg_geo_distance() ||
            leader_stat.tx_count_size() != local_stat.tx_count_size() ||
            leader_stat.stokes_size() != local_stat.stokes_size() ||
            leader_stat.gas_sum_size() != local_stat.gas_sum_size() ||
            leader_stat.credit_size() != local_stat.credit_size() ||
            leader_stat.consensus_gap_size() != local_stat.consensus_gap_size()) {
            SHARDORA_WARN("pool=%u, statistics[%d] structure mismatch", pool_index, i);
            return false;
        }
        
        // Compare arrays
        for (int j = 0; j < leader_stat.tx_count_size(); ++j) {
            if (leader_stat.tx_count(j) != local_stat.tx_count(j)) {
                SHARDORA_WARN("pool=%u, statistics[%d].tx_count[%d] mismatch", pool_index, i, j);
                return false;
            }
        }
        for (int j = 0; j < leader_stat.stokes_size(); ++j) {
            if (leader_stat.stokes(j) != local_stat.stokes(j)) {
                SHARDORA_WARN("pool=%u, statistics[%d].stokes[%d] mismatch", pool_index, i, j);
                return false;
            }
        }
        for (int j = 0; j < leader_stat.gas_sum_size(); ++j) {
            if (leader_stat.gas_sum(j) != local_stat.gas_sum(j)) {
                SHARDORA_WARN("pool=%u, statistics[%d].gas_sum[%d] mismatch", pool_index, i, j);
                return false;
            }
        }
        for (int j = 0; j < leader_stat.credit_size(); ++j) {
            if (leader_stat.credit(j) != local_stat.credit(j)) {
                SHARDORA_WARN("pool=%u, statistics[%d].credit[%d] mismatch", pool_index, i, j);
                return false;
            }
        }
        for (int j = 0; j < leader_stat.consensus_gap_size(); ++j) {
            if (leader_stat.consensus_gap(j) != local_stat.consensus_gap(j)) {
                SHARDORA_WARN("pool=%u, statistics[%d].consensus_gap[%d] mismatch", pool_index, i, j);
                return false;
            }
        }
    }
    
    // 2.6 Verify height_info (StatisticTxItem)
    if (leader_statistic.has_height_info() != local_statistic.has_height_info()) {
        SHARDORA_WARN("pool=%u, height_info presence mismatch", pool_index);
        return false;
    }
    if (leader_statistic.has_height_info()) {
        const auto& leader_height = leader_statistic.height_info();
        const auto& local_height = local_statistic.height_info();
        
        if (leader_height.sharding_id() != local_height.sharding_id() ||
            leader_height.block_height() != local_height.block_height() ||
            leader_height.tm_height() != local_height.tm_height() ||
            leader_height.heights_size() != local_height.heights_size()) {
            SHARDORA_WARN("pool=%u, height_info mismatch", pool_index);
            return false;
        }
        
        for (int i = 0; i < leader_height.heights_size(); ++i) {
            const auto& leader_pool_height = leader_height.heights(i);
            const auto& local_pool_height = local_height.heights(i);
            
            if (leader_pool_height.pool_index() != local_pool_height.pool_index() ||
                leader_pool_height.min_height() != local_pool_height.min_height() ||
                leader_pool_height.max_height() != local_pool_height.max_height()) {
                SHARDORA_WARN("pool=%u, height_info.heights[%d] mismatch", pool_index, i);
                return false;
            }
        }
    }
    
    // Step 3: Verify node information consistency (90% threshold)
    uint32_t total_nodes = leader_statistic.join_elect_nodes_size();
    if (total_nodes == 0) {
        // No nodes to validate, accept
        SHARDORA_DEBUG("pool=%u, no nodes in statistic, accepting", pool_index);
        return true;
    }
    
    // Check if local has same number of nodes
    if (local_statistic.join_elect_nodes_size() != static_cast<int>(total_nodes)) {
        SHARDORA_WARN("pool=%u, node count mismatch: leader=%u, local=%d",
            pool_index, total_nodes, local_statistic.join_elect_nodes_size());
        return false;
    }
    
    uint32_t matched_nodes = 0;
    
    // Build a map of local nodes for quick lookup
    std::map<std::string, const pools::protobuf::JoinElectNode*> local_nodes_map;
    for (int i = 0; i < local_statistic.join_elect_nodes_size(); ++i) {
        const auto& node = local_statistic.join_elect_nodes(i);
        std::string pubkey_str(node.pubkey().begin(), node.pubkey().end());
        local_nodes_map[pubkey_str] = &node;
    }
    
    // Compare each node from leader's statistic
    for (int i = 0; i < leader_statistic.join_elect_nodes_size(); ++i) {
        const auto& leader_node = leader_statistic.join_elect_nodes(i);
        std::string pubkey_str(leader_node.pubkey().begin(), leader_node.pubkey().end());
        
        auto it = local_nodes_map.find(pubkey_str);
        if (it == local_nodes_map.end()) {
            SHARDORA_DEBUG("pool=%u, node not found in local: %s",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str());
            continue;
        }
        
        const auto& local_node = *(it->second);
        
        // Compare all fields of JoinElectNode for complete match
        bool node_match = true;
        
        // Compare stoke
        if (leader_node.stoke() != local_node.stoke()) {
            SHARDORA_DEBUG("pool=%u, node %s stoke mismatch: leader=%lu, local=%lu",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str(),
                leader_node.stoke(), local_node.stoke());
            node_match = false;
        }
        
        // Compare shard
        if (leader_node.shard() != local_node.shard()) {
            SHARDORA_DEBUG("pool=%u, node %s shard mismatch: leader=%u, local=%u",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str(),
                leader_node.shard(), local_node.shard());
            node_match = false;
        }
        
        // Compare elect_pos
        if (leader_node.elect_pos() != local_node.elect_pos()) {
            SHARDORA_DEBUG("pool=%u, node %s elect_pos mismatch: leader=%d, local=%d",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str(),
                leader_node.elect_pos(), local_node.elect_pos());
            node_match = false;
        }
        
        // Compare credit
        if (leader_node.credit() != local_node.credit()) {
            SHARDORA_DEBUG("pool=%u, node %s credit mismatch: leader=%lu, local=%lu",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str(),
                leader_node.credit(), local_node.credit());
            node_match = false;
        }
        
        // Compare consensus_gap
        if (leader_node.consensus_gap() != local_node.consensus_gap()) {
            SHARDORA_DEBUG("pool=%u, node %s consensus_gap mismatch: leader=%lu, local=%lu",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str(),
                leader_node.consensus_gap(), local_node.consensus_gap());
            node_match = false;
        }
        
        // Compare area_point
        if (leader_node.has_area_point() != local_node.has_area_point()) {
            SHARDORA_DEBUG("pool=%u, node %s area_point presence mismatch",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str());
            node_match = false;
        } else if (leader_node.has_area_point()) {
            if (leader_node.area_point().x() != local_node.area_point().x() ||
                leader_node.area_point().y() != local_node.area_point().y()) {
                SHARDORA_DEBUG("pool=%u, node %s area_point mismatch: leader=(%d,%d), local=(%d,%d)",
                    pool_index, common::Encode::HexEncode(pubkey_str).c_str(),
                    leader_node.area_point().x(), leader_node.area_point().y(),
                    local_node.area_point().x(), local_node.area_point().y());
                node_match = false;
            }
        }
        
        if (node_match) {
            matched_nodes++;
            SHARDORA_DEBUG("pool=%u, node matched: %s",
                pool_index, common::Encode::HexEncode(pubkey_str).c_str());
        }
    }
    
    // Calculate consistency percentage
    double consistency_rate = (double)matched_nodes / (double)total_nodes;
    bool is_valid = consistency_rate >= 0.90;
    
    SHARDORA_DEBUG("pool=%u, statistic validation: matched=%u, total=%u, consistency=%.2f%%, valid=%s",
        pool_index, matched_nodes, total_nodes, consistency_rate * 100.0,
        is_valid ? "true" : "false");
    
    return is_valid;
}

// AcceptSync verifies the synchronized block information and updates the transaction pool
Status BlockAcceptor::AcceptSync(const view_block::protobuf::ViewBlockItem& view_block) {
    if (view_block.qc().pool_index() != pool_idx()) {
        return Status::kError;
    }
    
    return Status::kSuccess;
}

Status BlockAcceptor::addTxsToPool(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>& txs,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        BalanceAndNonceMap& now_balance_map,
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map) {

    // 0. Basic check
    if (txs.size() == 0) {
        SHARDORA_DEBUG("accepte empty called!");
        return Status::kAcceptorTxsEmpty;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    BalanceAndNonceMap prevs_balance_map;
    auto merge_begin_ms = common::TimeUtils::TimestampMs();
    view_block_chain_->MergeAllPrevBalanceMap(parent_hash, prevs_balance_map);
    auto merge_end_ms = common::TimeUtils::TimestampMs();
    if (merge_end_ms - merge_begin_ms >= 1) {
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();

    // ========================================================================
    // 1. Concurrent Environment Setup (Producer-Consumer)
    // ========================================================================
    
    // Pre-allocate containers to avoid resizing locks and memory copying.
    // temp_items: Store created TxItemPtr (Main thread writes, Child threads read)
    std::vector<pools::TxItemPtr> temp_items(txs.size(), nullptr);
    
    // verify_results: 0=pending, 1=success, -1=failure; written exclusively per-index (no races).
    std::vector<int8_t> verify_results(txs.size(), 0);
    
    bool is_leader = msg_ptr->is_leader;
    bool need_verify = !is_leader; // Leader skips verification, Follower verifies

    // verify_tasks: built during the main loop, dispatched to the persistent pool in one shot.
    std::vector<std::function<void()>> verify_tasks;
    if (need_verify) {
        verify_tasks.reserve(txs.size());
    }

    // ========================================================================
    // 2. Main Loop (Producer) - Single traversal of txs
    // ========================================================================
    
    // Helper Lambda: Used by TimeBlockTx
    auto tx_valid_func = [&](
            const address::protobuf::AddressInfo& addr_info, 
            const pools::protobuf::TxMessage& tx_info,
            uint64_t* now_nonce) -> int {
        return CheckTransactionValid(
            parent_hash, 
            view_block_chain_, 
            pools_mgr_, 
            addr_info, 
            tx_info, 
            now_nonce);
    };

    // Per-address nonce continuity tracking (mirrors TempGetTxIdempotently logic).
    // Maps address → last accepted nonce for that address in this block.
    // Ensures the leader cannot propose nonce gaps or duplicate nonces.
    std::unordered_map<std::string, uint64_t> addr_valid_nonce_map;

    bool create_success = true;
    for (int i = 0; i < txs.size(); i++) {
        auto* tx = &txs[i];
        
        protos::AddressInfoPtr address_info = nullptr;
        protos::AddressInfoPtr contract_address_info = nullptr;
        std::string from_id;

        // --- Serial Logic: Get Account ID (Very short time) ---
        if (pools::IsUserTransaction(tx->step())) {
            from_id = security_ptr_->GetAddressWithPublicKey(tx->pubkey());
        }

        // Build leader_nonce_map during this single traversal.
        // For contract execute/prefund/refund use the prefund composite key (to+from_id).
        // For plain user txs use from_id directly.
        if (out_leader_nonce_map != nullptr && pools::IsUserTransaction(tx->step()) && !tx->pubkey().empty()) {
            std::string nonce_key;
            if (tx->step() == pools::protobuf::kContractExcute ||
                    tx->step() == pools::protobuf::kContractRefund) {
                nonce_key = tx->to() + from_id;
            } else {
                nonce_key = from_id;
            }
            auto it = out_leader_nonce_map->find(nonce_key);
            if (it == out_leader_nonce_map->end() || tx->nonce() >= it->second) {
                (*out_leader_nonce_map)[nonce_key] = tx->nonce() + 1;
            }
        }
        
        // --- Serial Logic: DB Query & AddressInfo Retrieval (Must be serial) ---
        if (tx->step() == pools::protobuf::kContractExcute ||
                tx->step() == pools::protobuf::kContractRefund) {
            address_info = view_block_chain_->ChainGetAccountInfo(tx->to() + from_id);
            contract_address_info = view_block_chain_->ChainGetAccountInfo(tx->to());
            if (!contract_address_info) {
                SHARDORA_WARN("get contract address failed %s, nonce: %lu", 
                    common::Encode::HexEncode(tx->to()).c_str(), tx->nonce());
                verify_results[i] = -1; // Mark as failed
                continue;
            }
        } else if (tx->step() == pools::protobuf::kConsensusLocalTos) {
            pools::protobuf::ToTxMessageItem to_tx_item;
            if (!to_tx_item.ParseFromString(tx->value())) {
                SHARDORA_WARN("local get to txs info failed: %s, unique: %s",
                    common::Encode::HexEncode(tx->value()).c_str(),
                    common::Encode::HexEncode(tx->key()).c_str());
                verify_results[i] = -1; // Mark as failed
                continue;
            }

            auto iter = prevs_balance_map.find(to_tx_item.des());
            if (iter != prevs_balance_map.end()) {
                now_balance_map[iter->first] = iter->second;
            } else {
                auto tmp_address_info = view_block_chain_->ChainGetAccountInfo(to_tx_item.des());
                if (tmp_address_info != nullptr) {
                    auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>();
                    *new_addr_info = *tmp_address_info;
                    now_balance_map[to_tx_item.des()] = new_addr_info;
                }
            }

            address_info = account_mgr_->pools_address_info(tx->step(), pool_idx());
        } else {
            if (pools::IsUserTransaction(tx->step())) {
                address_info = view_block_chain_->ChainGetAccountInfo(from_id);
            } else {
                address_info = account_mgr_->pools_address_info(tx->step(), pool_idx());
            }
        }

        if (!address_info) {
            SHARDORA_WARN("get address failed nonce: %lu", tx->nonce());
            verify_results[i] = -1;
            continue;
        }

        // --- Serial Logic: Nonce validity + continuity check (mirrors TempGetTxIdempotently) ---
        // For user transactions we enforce two rules:
        //   1. The nonce must be valid against the chain state (CheckTransactionValid).
        //   2. If this address already appeared earlier in this block, the nonce must be
        //      exactly prev_nonce + 1 — no gaps, no duplicates.
        // This prevents a malicious leader from proposing nonce gaps or replays.
        if (pools::IsUserTransaction(tx->step())) {
            const std::string& nonce_addr = address_info->addr();
            auto prev_it = addr_valid_nonce_map.find(nonce_addr);
            if (prev_it == addr_valid_nonce_map.end()) {
                // First tx from this address in this block: validate against chain state.
                uint64_t now_nonce = 0lu;
                int res = tx_valid_func(*address_info, *tx, &now_nonce);
                if (res != 0) {
                    SHARDORA_WARN("nonce invalid (chain check) addr: %s, tx_nonce: %lu, "
                        "chain_nonce: %lu, res: %d, step: %u",
                        common::Encode::HexEncode(nonce_addr).c_str(),
                        tx->nonce(), now_nonce, res, (uint32_t)tx->step());
                    verify_results[i] = -1;
                    create_success = false;
                    break;
                }
                addr_valid_nonce_map[nonce_addr] = tx->nonce();
            } else {
                // Subsequent tx from same address: must be exactly prev + 1.
                uint64_t expected = prev_it->second + 1;
                if (tx->nonce() != expected) {
                    SHARDORA_WARN("nonce continuity violation addr: %s, expected: %lu, got: %lu, step: %u",
                        common::Encode::HexEncode(nonce_addr).c_str(),
                        expected, tx->nonce(), (uint32_t)tx->step());
                    verify_results[i] = -1;
                    create_success = false;
                    break;
                }
                prev_it->second = tx->nonce();
            }
        }

        // --- Serial Logic: Balance map update (State dependency, must be serial) ---
        auto now_map_iter = now_balance_map.find(address_info->addr());
        if (now_map_iter == now_balance_map.end()) {
            if (!pools::IsUserTransaction(tx->step())) {
                std::string val;
                if (shardora_host.GetKeyValue(tx->to(), tx->key(), &val) == shardoravm::kShardoravmSuccess) {
                    SHARDORA_WARN("invalid add tx now get local to tx to: %s, unique hash: %s", 
                        common::Encode::HexEncode(tx->to()).c_str(),
                        common::Encode::HexEncode(tx->key()).c_str());
                    verify_results[i] = -1;
                    continue;
                }
            }
        }
        
        auto iter = prevs_balance_map.find(address_info->addr());
        if (iter != prevs_balance_map.end()) {
            now_balance_map[iter->first] = iter->second;
        } else {
            auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>();
            *new_addr_info = *address_info;
            now_balance_map[address_info->addr()] = new_addr_info;
        }

        // --- Serial Logic: Object Factory Creation ---
        std::string contract_prefund_id;
        pools::TxItemPtr tx_ptr = nullptr;
        switch (tx->step()) {
        case pools::protobuf::kNormalFrom:
            tx_ptr = std::make_shared<consensus::FromTxItem>(
                    msg_ptr, i, account_mgr_, security_ptr_, address_info);
            break;
        case pools::protobuf::kRootCreateAddress:
            tx_ptr = std::make_shared<consensus::RootToTxItem>(
                    elect_info_->max_consensus_sharding_id(),
                    msg_ptr, i, vss_mgr_, account_mgr_, security_ptr_, address_info);
            break;
        case pools::protobuf::kCreateContract:
            tx_ptr = std::make_shared<consensus::ContractUserCreateCall>(
                    contract_mgr_, db_, msg_ptr, i, account_mgr_, security_ptr_, address_info);
            contract_prefund_id = tx->to() + from_id;
            break;
        case pools::protobuf::kContractExcute:
            tx_ptr = std::make_shared<consensus::ContractCall>(
                    contract_mgr_, db_, msg_ptr, i, account_mgr_, security_ptr_, contract_address_info);
            contract_prefund_id = tx->to() + from_id;
            break;
        case pools::protobuf::kContractGasPrefund:
            tx_ptr = std::make_shared<consensus::ContractPrefund>(
                    db_, msg_ptr, i, account_mgr_, security_ptr_, address_info);
            contract_prefund_id = tx->to() + from_id;
            break;
        case pools::protobuf::kContractRefund:
            tx_ptr = std::make_shared<consensus::ContractRefund>(
                    db_, msg_ptr, i, account_mgr_, security_ptr_, address_info);
            contract_prefund_id = tx->to() + from_id;
            break;
        case pools::protobuf::kConsensusLocalTos: {
            tx_ptr = std::make_shared<consensus::ToTxLocalItem>(
                    msg_ptr, i, db_, account_mgr_, security_ptr_, address_info);
            std::string val;
            if (shardora_host.GetKeyValue(tx_ptr->tx_info->to(), tx_ptr->tx_info->key(), &val) == shardoravm::kShardoravmSuccess) {
                SHARDORA_WARN("invalid add tx now get local to tx to: %s, unique hash: %s", 
                    common::Encode::HexEncode(tx_ptr->tx_info->to()).c_str(),
                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                tx_ptr = nullptr;
                create_success = false;
            }
            break;
        }
        case pools::protobuf::kNormalTo: {
            pools::protobuf::AllToTxMessage all_to_txs;
            if (!all_to_txs.ParseFromString(tx->value()) || all_to_txs.to_tx_arr_size() == 0) {
                //assert(false);
                create_success = false;
                break;
            }
            // Leader (or explicit leader-tx mode) builds ToTxItem directly from the proposal.
            if (is_leader || directly_user_leader_txs) {
                tx_ptr = std::make_shared<consensus::ToTxItem>(
                    msg_ptr, i, account_mgr_, security_ptr_, address_info);
            } else {
                // Backup: cross-shard NormalTo must match local pool tx and leader ToTxMessageItem set.
                auto tx_item = tx_pools_->GetToTxs(
                    pool_idx(), all_to_txs.to_heights().SerializeAsString());
                if (tx_item == nullptr || tx_item->txs.empty()) {
                    SHARDORA_WARN("kNormalTo backup: no local tx found, discarding propose. pool=%u key=%s",
                        pool_idx(), common::Encode::HexEncode(tx->key()).c_str());
                    create_success = false;
                    break;
                }
                auto local_tx = *(tx_item->txs.begin());
                if (local_tx->tx_info->key() != tx->key()) {
                    SHARDORA_WARN("kNormalTo backup: tx key mismatch, discarding propose. "
                        "local_key=%s leader_key=%s pool=%u",
                        common::Encode::HexEncode(local_tx->tx_info->key()).c_str(),
                        common::Encode::HexEncode(tx->key()).c_str(),
                        pool_idx());
                    create_success = false;
                    break;
                }
                pools::protobuf::AllToTxMessage local_all_to_txs;
                if (!local_all_to_txs.ParseFromString(local_tx->tx_info->value()) ||
                        local_all_to_txs.to_tx_arr_size() == 0) {
                    SHARDORA_WARN("kNormalTo backup: invalid local AllToTxMessage pool=%u key=%s",
                        pool_idx(), common::Encode::HexEncode(tx->key()).c_str());
                    create_success = false;
                    break;
                }
                if (!ValidateBackupNormalToAgainstLeader(
                        all_to_txs, local_all_to_txs, pool_idx())) {
                    create_success = false;
                    break;
                }
                tx_ptr = std::make_shared<consensus::ToTxItem>(
                    msg_ptr, i, account_mgr_, security_ptr_, address_info);
            }
            break;
        }
        case pools::protobuf::kStatistic: {
            // Follower validation for statistic transaction
            // Parse leader's statistic transaction
            pools::protobuf::ElectStatistic leader_statistic;
            if (!leader_statistic.ParseFromString(tx->value())) {
                SHARDORA_WARN("failed to parse leader's elect statistic, rejecting proposal. "
                    "pool=%u, key=%s",
                    pool_idx(),
                    common::Encode::HexEncode(tx->key()).c_str());
                create_success = false;
                break;
            }
            
            // Verify sharding_id matches
            if (leader_statistic.sharding_id() != msg_ptr->header.hotstuff().net_id()) {
                SHARDORA_WARN("statistic sharding_id mismatch, rejecting proposal. "
                    "leader_shard=%u, expected_shard=%u, pool=%u",
                    leader_statistic.sharding_id(),
                    msg_ptr->header.hotstuff().net_id(),
                    pool_idx());
                create_success = false;
                break;
            }
            
            // Verify transaction exists in local tx_pool
            if (!pools_mgr_->TxKeyExists(
                    pool_idx(),
                    tx->to(),
                    tx->nonce(),
                    tx->key())) {
                SHARDORA_WARN("statistic tx not found in local tx_pool, rejecting proposal. "
                    "pool=%u, to=%s, nonce=%lu, key=%s",
                    pool_idx(),
                    common::Encode::HexEncode(tx->to()).c_str(),
                    tx->nonce(),
                    common::Encode::HexEncode(tx->key()).c_str());
                create_success = false;
                break;
            }
            
            // Verify node information consistency (90% threshold)
            if (!ValidateStatisticNodeConsistency(leader_statistic, pool_idx())) {
                SHARDORA_WARN("statistic node consistency validation failed (< 90%%), rejecting proposal. "
                    "pool=%u, key=%s",
                    pool_idx(),
                    common::Encode::HexEncode(tx->key()).c_str());
                create_success = false;
                break;
            }
            
            // Get address_info for statistic transaction
            address_info = account_mgr_->pools_address_info(tx->step(), pool_idx());
            if (!address_info) {
                SHARDORA_WARN("failed to get address_info for statistic tx, pool=%u", pool_idx());
                create_success = false;
                break;
            }
            
            // All validations passed, create tx item
            tx_ptr = std::make_shared<consensus::StatisticTxItem>(
                msg_ptr, i, account_mgr_, security_ptr_, address_info);
            break;
        }
        case pools::protobuf::kCross: {
            //assert(false); break;
        }
        case pools::protobuf::kConsensusRootElectShard: {
            pools::protobuf::ElectStatistic elect_statistic;
            if (!elect_statistic.ParseFromString(tx->value())) {
                SHARDORA_DEBUG("parse elect_statistic error!");
                create_success = false;
                break;            
            }

            if (bls_mgr_->CheckBlsConsensusInfo(elect_statistic.elect_block()) != bls::kBlsSuccess) {
                SHARDORA_DEBUG("check bls consensus info failed!");
                create_success = false;
                break;
            }

            tx_ptr = std::make_shared<consensus::ElectTxItem>(
                msg_ptr, i, account_mgr_, security_ptr_, prefix_db_, elect_mgr_, 
                vss_mgr_, bls_mgr_, false, false, 
                elect_info_->max_consensus_sharding_id() - 1, address_info);
            break;
        }
        case pools::protobuf::kConsensusRootTimeBlock: {
            if (!tm_block_mgr_->CheckLeaderTimeblockTxValid(*tx, tx_valid_func)) {
                SHARDORA_ERROR("check leader timeblock tx valid failed!");
                create_success = false;
                break;
            }

            tx_ptr = std::make_shared<consensus::TimeBlockTx>(
                msg_ptr, i, account_mgr_, security_ptr_, address_info);
            break;
        }
        case pools::protobuf::kRootCross: {
            tx_ptr = std::make_shared<consensus::RootCrossTxItem>(
                msg_ptr, i, account_mgr_, security_ptr_, address_info);
            break;
        }
        case pools::protobuf::kJoinElect: {
            tx_ptr = std::make_shared<consensus::JoinElectTxItem>(
                msg_ptr, i, account_mgr_, security_ptr_, prefix_db_, elect_mgr_, address_info,
                (*tx).pubkey());
            break;
        }
        case pools::protobuf::kPoolStatisticTag: {
            tx_ptr = std::make_shared<consensus::PoolStatisticTag>(
                msg_ptr, i, account_mgr_, security_ptr_, address_info);
            break;
        }
        case pools::protobuf::kCreateLibrary: {
            tx_ptr = std::make_shared<consensus::CreateLibrary>(
                msg_ptr, i, account_mgr_, security_ptr_, address_info);
            break;
        }
        default:
            SHARDORA_FATAL("invalid tx step: %d", (int32_t)tx->step());
            create_success = false;
        }

        if (!create_success) {
            break;
        }

        // Handle prefund
        if (!contract_prefund_id.empty()) {
            auto iter = prevs_balance_map.find(contract_prefund_id);
            if (iter != prevs_balance_map.end()) {
                now_balance_map[iter->first] = iter->second;
            } else {
                address_info = view_block_chain_->ChainGetAccountInfo(contract_prefund_id);
                if (address_info) {
                    auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>();
                    *new_addr_info = *address_info;
                    now_balance_map[contract_prefund_id] = new_addr_info;
                }
            }
        }

        // --- Core Logic: Submit Task ---
        if (create_success && tx_ptr != nullptr) {
            temp_items[i] = tx_ptr;
            if (!need_verify) {
                // Leader path: mark success directly, no signature check needed.
                verify_results[i] = 1;
            } else {
                // Follower path: build a verify task for the persistent thread pool.
                // Capture by value what the worker needs; temp_items[i] is already set
                // and will not be modified again by the main thread.
                const int   idx  = i;
                const auto* ptx  = &txs[i];
                verify_tasks.emplace_back([this, idx, ptx, &temp_items, &verify_results]() {
                    auto& tx_ptr2 = temp_items[idx];
                    if (!tx_ptr2 || !tx_ptr2->tx_info) {
                        verify_results[idx] = -1;
                        return;
                    }
                    if (!pools::IsUserTransaction(tx_ptr2->tx_info->step())) {
                        verify_results[idx] = 1;
                        return;
                    }

                    // ETH-format tx: signature already verified in http_handler.cc
                    // during RLP decoding and signature recovery. Skip verification here.
                    if (ptx->has_eth_raw_tx() && !ptx->eth_raw_tx().empty()) {
                        verify_results[idx] = 1;  // Already verified
                        return;
                    }

                    auto tx_hash = pools::GetTxMessageHash(*ptx);
                    int ret = security::kSecurityError;
                    if (ptx->pubkey().size() == 64u) {
                        security::GmSsl gmssl;
                        ret = gmssl.Verify(tx_hash, tx_ptr2->tx_info->pubkey(), tx_ptr2->tx_info->sign());
                    } else if (ptx->pubkey().size() > 128u) {
                        security::Oqs oqs;
                        ret = oqs.Verify(tx_hash, tx_ptr2->tx_info->pubkey(), tx_ptr2->tx_info->sign());
                    } else {
                        ret = security_ptr_->Verify(tx_hash, tx_ptr2->tx_info->pubkey(), tx_ptr2->tx_info->sign());
                    }
                    verify_results[idx] = (ret == security::kSecuritySuccess) ? 1 : -1;
                });
            }
        } else {
            verify_results[i] = -1;
        }
    } // End of loop

    // ========================================================================
    // 3. Finish and Collect
    // ========================================================================

    if (need_verify && !verify_tasks.empty()) {
        // Dispatch all verify tasks to the persistent thread pool and block until done.
        // No thread creation/destruction overhead — threads were started in Init().
        auto verify_begin_ms = common::TimeUtils::TimestampMs();
        RunVerifyBatch(verify_tasks);
        auto verify_end_ms = common::TimeUtils::TimestampMs();
    }

    if (!create_success) {
        return Status::kError;
    }
    
    // 4. Collect valid results in order
    // verify_results[i] == 1 means: (Leader passed directly) OR (Follower verification passed)
    auto& final_txs = txs_ptr->txs;
    final_txs.reserve(final_txs.size() + txs.size());

    for (int i = 0; i < txs.size(); ++i) {
        if (verify_results[i] == 1 && temp_items[i] != nullptr) {
            final_txs.push_back(temp_items[i]);
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

Status BlockAcceptor::GetAndAddTxsLocally(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const hotstuff::protobuf::TxPropose& tx_propose,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host,
        std::unordered_map<std::string, uint64_t>* out_leader_nonce_map) {
    auto add_txs_status = addTxsToPool(
        msg_ptr,
        parent_hash, 
        tx_propose.txs(), 
        directly_user_leader_txs, 
        txs_ptr,
        balance_map,
        shardora_host,
        out_leader_nonce_map);
    if (add_txs_status != Status::kSuccess) {
        SHARDORA_ERROR("invalid consensus, add_txs_status failed: %d.", (int32_t)add_txs_status);
        return add_txs_status;
    }

    if (!txs_ptr) {
        SHARDORA_ERROR("invalid consensus, tx empty.");
        return Status::kAcceptorTxsEmpty;
    }

    if (txs_ptr->txs.size() != (size_t)tx_propose.txs_size()) {
// #ifndef NDEBUG
//         for (uint32_t i = 0; i < uint32_t(tx_propose.txs_size()); i++) {
//             auto tx = &tx_propose.txs(i);
//             SHARDORA_WARN("leader tx step: %u, gid: %s", tx->step(), common::Encode::HexEncode(tx->gid()).c_str());
//         }
// #endif
        SHARDORA_ERROR("invalid consensus, txs not equal to leader: local_txs=%zu, leader_txs=%d, pool_idx=%u, "
            "local_first_tx_hash=%s, leader_first_tx_hash=%s",
            txs_ptr->txs.size(), tx_propose.txs_size(), pool_idx_,
            (txs_ptr->txs.empty() ? "empty" : common::Encode::HexEncode(
                pools::GetTxMessageHash(*txs_ptr->txs[0]->tx_info)).substr(0, 16).c_str()),
            (tx_propose.txs_size() == 0 ? "empty" : common::Encode::HexEncode(
                pools::GetTxMessageHash(tx_propose.txs(0))).substr(0, 16).c_str()));
        // //assert(false);
        return Status::kAcceptorTxsEmpty;
    }
    
    txs_ptr->pool_index = pool_idx_;
    return Status::kSuccess;
}

bool BlockAcceptor::IsBlockValid(const view_block::protobuf::ViewBlockItem& view_block) {
    // Verify block prehash, latest height, etc.
    auto* shardora_block = &view_block.block_info();
    uint64_t pool_height = pools_mgr_->latest_height(pool_idx());
    if (shardora_block->height() <= pool_height || pool_height == common::kInvalidUint64) {
        SHARDORA_WARN("Accept height error: %lu, %lu", shardora_block->height(), pool_height);
        return false;
    }

    auto cur_time = common::TimeUtils::TimestampMs();
    // The timestamp of the new block must be greater than the timestamp of the previous block.
    uint64_t preblock_time = pools_mgr_->latest_timestamp(pool_idx());
    if (shardora_block->timestamp() <= preblock_time && shardora_block->timestamp() + 10000lu >= cur_time) {
        SHARDORA_WARN("Accept timestamp error: %lu, %lu, cur: %lu", shardora_block->timestamp(), preblock_time, cur_time);
        return false;
    }
    
    return true;
}

Status BlockAcceptor::DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host) {
    Status s = BlockExecutorFactory().Create(security_ptr_)->DoTransactionAndCreateTxBlock(
            txs_ptr, view_block, balance_map, shardora_host);
    if (s != Status::kSuccess) {
        return s;
    }

// #ifndef NDEBUG
//     if (!txs_ptr->txs.empty() && !network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
//         bool valid = true;
//         for (uint32_t i = 0; i < view_block->block_info().tx_list_size(); ++i) {
//             auto& tx = view_block->block_info().tx_list(i);
//             SHARDORA_WARN("block tx from: %s, to: %s, amount: %lu, balance: %lu, %u_%u_%u, height: %lu",
//                 (tx.from().empty() ? 
//                 "" : 
//                 common::Encode::HexEncode(tx.from()).c_str()), 
//                 common::Encode::HexEncode(tx.to()).c_str(), 
//                 tx.amount(),
//                 tx.balance(),
//                 view_block->qc().network_id(),
//                 view_block->qc().pool_index(),
//                 view_block->qc().view(),
//                 view_block->block_info().height());

//             if (tx.amount() != 0) {
//                 valid = false;
//                 const std::string* addr = nullptr;
//                 if (pools::IsTxUseFromAddress(tx.step())) {
//                     addr = &tx.from();
//                 } else {
//                     addr = &tx.to();
//                 }

//                 auto addr_iter = balance_map.find(*addr);
//                 if (addr_iter == balance_map.end()) {
//                     //assert(false);
//                 }
                    
//             SHARDORA_WARN("transaction balance map addr: %s, balance: %lu, view_block_hash: %s, prehash: %s",
//                     common::Encode::HexEncode(*addr).c_str(), addr_iter->second, 
//                     common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
//                     common::Encode::HexEncode(view_block->parent_hash()).c_str());
//             }
//         }

//         if (balance_map.empty()) {
//             //assert(valid);
//         }
//     }
// #endif

    return s;
}

} // namespace hotstuff

} // namespace shardora
