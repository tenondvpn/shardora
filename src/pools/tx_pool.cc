#include "pools/tx_pool.h"
#include <cassert>
#include <common/log.h>

#include "common/encode.h"
#include "common/utils.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"
#include "pools/tx_utils.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/tcp_transport.h"

namespace shardora {

namespace pools {
    

static transport::MessagePtr CreateTransactionWithAttr(
        std::shared_ptr<security::Security>& security,
        uint64_t nonce,
        const std::string& from_prikey,
        const std::string& to,
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    // auto* brd = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(security->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kNormalFrom);
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    if (!key.empty()) {
        if (key == "create_contract") {
            new_tx->set_step(pools::protobuf::kContractCreate);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractGasPrepayment);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(val);
        } else {
            new_tx->set_key(key);
            if (!val.empty()) {
                new_tx->set_value(val);
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx); // cout 输出信息
    std::string sign;
    if (security->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }

    new_tx->set_sign(sign);
    assert(new_tx->gas_price() > 0);
    return msg_ptr;
}


static std::unordered_map<std::string, std::string> g_pri_addrs_map;
static std::vector<std::string> g_prikeys;
static std::vector<std::string> g_addrs;
static std::unordered_map<std::string, std::string> g_pri_pub_map;
static std::vector<std::string> g_oqs_prikeys;
static std::unordered_map<std::string, std::string> g_oqs_pri_pub_map;
static std::unordered_map<std::string, uint64_t> prikey_with_nonce;
static std::unordered_map<std::string, std::shared_ptr<address::protobuf::AddressInfo>> address_map;
std::shared_ptr<address::protobuf::AddressInfo> address_[common::kInvalidPoolIndex];
std::shared_ptr<security::Security> pool_sec[common::kInvalidPoolIndex];

static void LoadAllAccounts(int32_t shardnum=3) {
    FILE* fd = fopen((std::string("/root/shardora/init_accounts") + std::to_string(shardnum)).c_str(), "r");
    if (fd == nullptr) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
    }

    bool res = true;
    std::string filed;
    const uint32_t kMaxLen = 1024;
    char* read_buf = new char[kMaxLen];
    while (true) {
        char* read_res = fgets(read_buf, kMaxLen, fd);
        if (read_res == NULL) {
            break;
        }

        std::string prikey = common::Encode::HexDecode(std::string(read_res, 64));
        g_prikeys.push_back(prikey);
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        g_pri_pub_map[prikey] = security->GetPublicKey();
        std::string addr = security->GetAddress();
        g_pri_addrs_map[prikey] = addr;
        g_addrs.push_back(addr);
        if (g_pri_addrs_map.size() >= common::kImmutablePoolSize) {
            break;
        }
        std::cout << common::Encode::HexEncode(prikey) << " : " << common::Encode::HexEncode(addr) << std::endl;
    }

    assert(!g_prikeys.empty());
    while (g_prikeys.size() < common::kImmutablePoolSize) {
        g_prikeys.push_back(g_prikeys[0]);
    }

    fclose(fd);
    delete[]read_buf;
}

TxPool::TxPool() {}

TxPool::~TxPool() {}

void TxPool::Init(
        TxPoolManager* pools_mgr,
        std::shared_ptr<security::Security> security,
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    pools_mgr_ = pools_mgr;
    security_ = security;
    kv_sync_ = kv_sync;
    pool_index_ = pool_idx;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    InitLatestInfo();
    InitHeightTree();
}

void TxPool::InitHeightTree() {
    CheckThreadIdValid();
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    auto height_tree_ptr = std::make_shared<HeightTreeLevel>(
        net_id,
        pool_index_,
        latest_height_,
        db_);
    height_tree_ptr->Set(0);
    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr->Valid(synced_height_ + 1)) {
            break;
        }
    }

    height_tree_ptr_ = height_tree_ptr;
    LoadAllAccounts(3);
    auto i = pool_index_;
    auto from_prikey = g_prikeys[i];
    std::shared_ptr<security::Security> thread_security = std::make_shared<security::Ecdsa>();
    for (uint32_t tmp_idx = 0; tmp_idx < g_prikeys.size(); ++tmp_idx) {
        from_prikey = g_prikeys[tmp_idx];
        thread_security->SetPrivateKey(from_prikey);
        if (common::GetAddressPoolIndex(thread_security->GetAddress()) == i) {
            break;
        }
    }

    pool_sec[i] = thread_security;
    address_map[from_prikey] = prefix_db_->GetAddressInfo(thread_security->GetAddress());
    prikey_with_nonce[from_prikey] = address_map[from_prikey]->nonce();
}

uint32_t TxPool::SyncMissingBlocks(uint64_t now_tm_ms) {
    if (!height_tree_ptr_) {
        ZJC_DEBUG("get invalid height_tree_ptr_ size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     if (prev_synced_time_ms_ >= now_tm_ms) {
//         return 0;
//     }
// 
    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        ZJC_DEBUG("get invalid heights size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
    ZJC_DEBUG("%u get invalid heights size: %u, latest_height_: %lu", 
        pool_index_, invalid_heights.size(), latest_height_);
    if (invalid_heights.size() > 0) {
        auto net_id = common::GlobalInfo::Instance()->network_id();
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
            return 0;
        }

        uint64_t min_height = invalid_heights[0];
        uint32_t synced_count = 0;
        for (uint64_t i = min_height; i < latest_height_; ++i) {
            if (prefix_db_->BlockExists(net_id, pool_index_, i)) {
                ZJC_DEBUG("block exists now add sync height 1, %u_%u_%lu", 
                    net_id,
                    pool_index_,
                    i);
                height_tree_ptr_->Set(i);
                continue;
            }

            ZJC_DEBUG("now add sync height 1, %u_%u_%lu", 
                net_id,
                pool_index_,
                i);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                i,
                sync::kSyncHigh);
            ++synced_count;
            if (synced_count >= 128u) {
                break;
            }
        }
    }

    return invalid_heights.size();
}

int TxPool::AddTx(TxItemPtr& tx_ptr) {
    if (IsUserTransaction(tx_ptr->tx_info->step()) && 
            added_txs_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_DEBUG("add failed extend %u, %u, all valid: %u", 
            added_txs_.size(), common::GlobalInfo::Instance()->each_tx_pool_max_txs(), all_tx_size());
        return kPoolsError;
    }

    if (tx_ptr->tx_key.empty()) {
        ZJC_DEBUG("add failed unique hash empty: %d", tx_ptr->tx_info->step());
        tx_ptr->tx_key = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    if (!IsUserTransaction(tx_ptr->tx_info->step()) && !tx_ptr->tx_info->key().empty()) {
        ZJC_DEBUG("success add system tx step: %d, nonce: %lu, unique hash: %s", 
            tx_ptr->tx_info->step(), 
            tx_ptr->tx_info->nonce(), 
            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
        if (over_unique_hash_set_.find(tx_ptr->tx_info->key()) != over_unique_hash_set_.end()) {
            ZJC_DEBUG("trace tx pool: %d, failed add tx %s, key: %s, "
                "nonce: %lu, step: %d, unique hash exists: %s", 
                pool_index_,
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(), 
                tx_ptr->tx_info->nonce(),
                tx_ptr->tx_info->step(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
            return kPoolsError;
        }
    }

    added_txs_.push(tx_ptr);
    ZJC_DEBUG("trace tx pool: %d, success add tx %s, key: %s, nonce: %lu, step: %d", 
        pool_index_,
        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(), 
        tx_ptr->tx_info->nonce(),
        tx_ptr->tx_info->step());
    if (tx_ptr->tx_info->step() == pools::protobuf::kContractExcute) {
        assert(tx_ptr->address_info->addr().size() == common::kPreypamentAddressLength);
    }
    
    return kPoolsSuccess;
}

void TxPool::TxOver(view_block::protobuf::ViewBlockItem& view_block) {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    for (uint32_t i = 0; i < view_block.block_info().tx_list_size(); ++i) {
        auto addr = IsTxUseFromAddress(view_block.block_info().tx_list(i).step()) ? 
            view_block.block_info().tx_list(i).from() : 
            view_block.block_info().tx_list(i).to();
        if (!IsUserTransaction(view_block.block_info().tx_list(i).step()) && 
                !view_block.block_info().tx_list(i).unique_hash().empty()) {
            addr = std::to_string(view_block.block_info().tx_list(i).step());
        }

        if (view_block.block_info().tx_list(i).step() == pools::protobuf::kContractExcute) {
            addr = view_block.block_info().tx_list(i).to() + view_block.block_info().tx_list(i).from();
        }

        if (addr.empty()) {
            ZJC_DEBUG("addr is empty: %s", ProtobufToJson(view_block.block_info().tx_list(i)).c_str());
            assert(false);
            continue;
        }

        auto remove_tx_func = [&](std::map<std::string, std::map<uint64_t, TxItemPtr>>& tx_map) {
            auto tx_iter = tx_map.find(addr);
            if (tx_iter != tx_map.end()) {
                for (auto nonce_iter = tx_iter->second.begin(); nonce_iter != tx_iter->second.end(); ) {
                    ZJC_DEBUG("find tx addr success: %s, unique hash: %s, "
                        "step: %lu, nonce: %lu, consensus nonce: %lu, key: %s", 
                        common::Encode::HexEncode(addr).c_str(),
                        common::Encode::HexEncode(view_block.block_info().tx_list(i).unique_hash()).c_str(),
                        view_block.block_info().tx_list(i).step(),
                        view_block.block_info().tx_list(i).nonce(),
                        nonce_iter->second->tx_info->nonce(),
                        common::Encode::HexEncode(nonce_iter->second->tx_info->key()).c_str());
                    ZJC_DEBUG("find tx addr success: %s, nonce: %lu, remove nonce: %lu", 
                        common::Encode::HexEncode(addr).c_str(), 
                        nonce_iter->first,
                        view_block.block_info().tx_list(i).nonce());
                    if (!IsUserTransaction(view_block.block_info().tx_list(i).step())) {
                        if (nonce_iter->second->tx_info->key() != view_block.block_info().tx_list(i).unique_hash()) {
                            ++nonce_iter;
                            continue;
                        }

                        over_unique_hash_set_.insert(view_block.block_info().tx_list(i).unique_hash());
                        ZJC_DEBUG("trace tx pool: %d, success add unique tx %s, key: %s, "
                            "nonce: %lu, step: %d, unique hash exists: %s", 
                            pool_index_,
                            common::Encode::HexEncode(view_block.block_info().tx_list(i).to()).c_str(), 
                            common::Encode::HexEncode(view_block.block_info().tx_list(i).unique_hash()).c_str(), 
                            view_block.block_info().tx_list(i).nonce(),
                            view_block.block_info().tx_list(i).step(),
                            common::Encode::HexEncode(addr).c_str());
                    } else {
                        if (nonce_iter->first > view_block.block_info().tx_list(i).nonce()) {
                            break;
                        }
                    }
                    
                    if (IsUserTransaction(view_block.block_info().tx_list(i).step())) {
                        ++all_delay_tx_count_;
                        all_delay_tm_us_ += now_tm_us - nonce_iter->second->receive_tm_us;
                    }

                    ZJC_DEBUG("trace tx pool: %d, over tx addr: %s, nonce: %lu", 
                        pool_index_,
                        common::Encode::HexEncode(addr).c_str(), 
                        nonce_iter->first);
                    auto tx_ptr = nonce_iter->second;
                    ZJC_DEBUG("over pop success add system tx nonce addr: %s, "
                        "addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                        tx_ptr->address_info->nonce(), 
                        tx_ptr->tx_info->nonce(),
                        common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                    nonce_iter = tx_iter->second.erase(nonce_iter);
                }

                if (tx_iter->second.empty()) {
                    tx_map.erase(tx_iter);
                }
            } else {
                ZJC_DEBUG("find tx addr failed: %s", common::Encode::HexEncode(addr).c_str());
            }
        };
        
        remove_tx_func(system_tx_map_);
        remove_tx_func(tx_map_);
        remove_tx_func(consensus_tx_map_);
        ZJC_DEBUG("trace tx pool: %d, step: %d, to: %s, unique hash: %s, over tx addr: %s, nonce: %lu", 
            pool_index_,
            view_block.block_info().tx_list(i).step(),
            common::Encode::HexEncode(view_block.block_info().tx_list(i).to()).c_str(), 
            common::Encode::HexEncode(view_block.block_info().tx_list(i).unique_hash()).c_str(), 
            common::Encode::HexEncode(addr).c_str(), 
            view_block.block_info().tx_list(i).nonce());
    }
        
    if (prev_delay_tm_timeout_ + 3000lu <= (now_tm_us / 1000lu) && all_delay_tx_count_ > 0) {
        ZJC_WARN("pool: %d, average delay us: %lu",
            pool_index_, (all_delay_tm_us_ / all_delay_tx_count_));
        all_delay_tm_us_ = 0;
        all_delay_tx_count_ = 0;
        prev_delay_tm_timeout_ = now_tm_us / 1000lu;
        common::GlobalInfo::Instance()->set_global_latency(all_delay_tm_us_ / all_delay_tx_count_);
    }

}

void TxPool::GetTxSyncToLeader(
        uint32_t leader_idx, 
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    TxItemPtr tx_ptr;
    while (added_txs_.pop(&tx_ptr)) {
        ZJC_DEBUG("pop success add system tx nonce addr: %s, addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
         if (!IsUserTransaction(tx_ptr->tx_info->step())) {
            system_tx_map_[std::to_string(tx_ptr->tx_info->step())][tx_ptr->tx_info->nonce()] = tx_ptr;
            ZJC_DEBUG("success add system tx nonce addr: %s, addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
            continue;
        }

        if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
            ZJC_DEBUG("failed get tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce());
            continue;
        }

        auto iter = consensus_tx_map_.find(tx_ptr->address_info->addr());
        if (iter != consensus_tx_map_.end()) {
            auto nonce_iter = iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != iter->second.end()) {
                ZJC_DEBUG("exists failed add tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce());
                continue;
            }
        }

        tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        ZJC_DEBUG("success add tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
            tx_ptr->address_info->nonce(), 
            tx_ptr->tx_info->nonce());
    }

    for (auto iter = tx_map_.begin(); iter != tx_map_.end(); ++iter) {
        uint64_t valid_nonce = common::kInvalidUint64;
        for (auto nonce_iter = iter->second.begin(); nonce_iter != iter->second.end(); ++nonce_iter) {
            auto tx_ptr = nonce_iter->second;
            if (tx_ptr->synced_leaders_.Valid(leader_idx)) {
                continue;
            }

            if (valid_nonce == common::kInvalidUint64) {
                int res = tx_valid_func(
                        *tx_ptr->address_info, 
                        *tx_ptr->tx_info);
                if (res != 0) {
                    if (res > 0) {
                        continue;
                    }
                    
                    ZJC_DEBUG("tx_key invalid: %s",
                        common::Encode::HexEncode(tx_ptr->tx_key).c_str());
                    break;
                }
            } else {
                if (valid_nonce + 1 != tx_ptr->tx_info->nonce()) {
                    break;
                }
            }

            valid_nonce = tx_ptr->tx_info->nonce();
            tx_ptr->synced_leaders_.Set(leader_idx);
            if (!IsUserTransaction(tx_ptr->tx_info->step())) {
                ZJC_DEBUG("nonce invalid: %lu, step is not user tx: %d", 
                    tx_ptr->tx_info->nonce(), 
                    tx_ptr->tx_info->step());
            } else {
                ZJC_DEBUG("trace tx pool: %d, to leader tx addr: %s, nonce: %lu", 
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                    tx_ptr->tx_info->nonce());
                auto* tx = txs->Add();
                *tx = *tx_ptr->tx_info;
                if (txs->size() >= count) {
                    break;
                }
            }
        }

        if (txs->size() >= count) {
            break;
        }
    }
}

void TxPool::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::vector<pools::TxItemPtr>& res_map,
        uint32_t count,
        pools::CheckAddrNonceValidFunction tx_valid_func) {
#ifdef USE_SERVER_TEST_TRANSACTION
    if (common::GlobalInfo::Instance()->test_pool_index() >= 0) {
        auto i = pool_index_;
        for (uint32_t tx_idx = 0; tx_idx < send_out_tps; ++tx_idx) {
            auto from_prikey = pool_sec[i]->GetPrikey();
            auto tx_msg_ptr = CreateTransactionWithAttr(
                pool_sec[i],
                ++prikey_with_nonce[from_prikey],
                from_prikey,
                to,
                "",
                "",
                1980,
                10000,
                1,
                3);
            tx_msg_ptr->address_info = address_map[from_prikey];
            pools::TxItemPtr tx_ptr = pools_mgr_->CreateTxPtr(tx_msg_ptr);
            if (tx_ptr == nullptr) {
                assert(false);
                return;
            }
        
            res_map.push_back(tx_ptr);
            
            // tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
            // consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
            ZJC_DEBUG("success add tx nonce addr: %s, addr nonce: %lu, tx nonce: %lu",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce());
            if (res_map.size() >= count) {
                return;
            }
        }
                
        return;
    }
#endif


    TxItemPtr tx_ptr;
    while (added_txs_.pop(&tx_ptr)) {
        ZJC_DEBUG("pop success add system tx nonce addr: %s, "
                "addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
        if (!IsUserTransaction(tx_ptr->tx_info->step())) {
            system_tx_map_[std::to_string(tx_ptr->tx_info->step())][tx_ptr->tx_info->nonce()] = tx_ptr;
            ZJC_DEBUG("success add system tx nonce addr: %s, "
                "addr nonce: %lu, tx nonce: %lu, unique hash: %s",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce(),
                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
            continue;
        }

        if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
            ZJC_DEBUG("failed get tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                tx_ptr->address_info->nonce(), 
                tx_ptr->tx_info->nonce());
            continue;
        }

        auto iter = consensus_tx_map_.find(tx_ptr->address_info->addr());
        if (iter != consensus_tx_map_.end()) {
            auto nonce_iter = iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != iter->second.end()) {
                ZJC_DEBUG("exists failed get tx nonce invalid addr: %s, addr nonce: %lu, tx nonce: %lu",
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
                    tx_ptr->address_info->nonce(), 
                    tx_ptr->tx_info->nonce());
                continue;
            }
        }

        tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
        ZJC_DEBUG("success add tx nonce addr: %s, addr nonce: %lu, tx nonce: %lu",
            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(),
            tx_ptr->address_info->nonce(), 
            tx_ptr->tx_info->nonce());
    }

    while (consensus_added_txs_.pop(&tx_ptr)) {
        if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
            continue;
        }

        auto iter = consensus_tx_map_.find(tx_ptr->address_info->addr());
        if (iter != consensus_tx_map_.end()) {
            auto nonce_iter = iter->second.find(tx_ptr->tx_info->nonce());
            if (nonce_iter != iter->second.end()) {
                continue;
            }
        }

        consensus_tx_map_[tx_ptr->address_info->addr()][tx_ptr->tx_info->nonce()] = tx_ptr;
    }

    std::set<uint32_t> system_added_step;
    auto get_tx_func = [&](std::map<std::string, std::map<uint64_t, TxItemPtr>>& tx_map) {
        for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
            uint64_t valid_nonce = common::kInvalidUint64;
            for (auto nonce_iter = iter->second.begin(); nonce_iter != iter->second.end(); ++nonce_iter) {
                auto tx_ptr = nonce_iter->second;
                if (!IsUserTransaction(tx_ptr->tx_info->step())) {
                    auto iter = system_added_step.find(tx_ptr->tx_info->step());
                    if (iter != system_added_step.end()) {
                        continue;
                    }

                    system_added_step.insert(tx_ptr->tx_info->step());
                }
                
                if (valid_nonce == common::kInvalidUint64) {
                    int res = tx_valid_func(
                        *tx_ptr->address_info, 
                        *tx_ptr->tx_info);
                    if (res != 0) {
                        if (res > 0) {
                            ZJC_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s",
                                pool_index_,
                                common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                                tx_ptr->tx_info->nonce(),
                                common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                            continue;
                        }
                        
                        ZJC_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s",
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                            tx_ptr->tx_info->nonce(),
                            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                        break;
                    }
                } else {
                    if (tx_ptr->tx_info->nonce() != valid_nonce + 1) {
                        ZJC_DEBUG("trace tx pool: %d, tx_key invalid addr: %s, nonce: %lu, unique hash: %s",
                            pool_index_,
                            common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                            tx_ptr->tx_info->nonce(),
                            common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str());
                        break;
                    }
                }

                valid_nonce = tx_ptr->tx_info->nonce();
                res_map.push_back(tx_ptr);
                ZJC_DEBUG("trace tx pool: %d, consensus leader tx addr: %s, key: %s, nonce: %lu, "
                    "res count: %u, count: %u, tx_map size: %u, addr tx size: %u", 
                    pool_index_,
                    common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
                    common::Encode::HexEncode(tx_ptr->tx_info->key()).c_str(), 
                    tx_ptr->tx_info->nonce(),
                    res_map.size(),
                    count,
                    tx_map.size(),
                    iter->second.size());
                if (res_map.size() >= count) {
                    break;
                }
            }

            if (res_map.size() >= count) {
                break;
            }
        }
    };

    get_tx_func(system_tx_map_);
    get_tx_func(consensus_tx_map_);
}

void TxPool::InitLatestInfo() {
    pools::protobuf::PoolLatestInfo pool_info;
    uint32_t network_id = common::GlobalInfo::Instance()->network_id();
    if (network_id == common::kInvalidUint32) {
        return;
    }

    if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id < network::kConsensusWaitingShardEndNetworkId) {
        network_id -= network::kConsensusWaitingShardOffset;
    }

    if (prefix_db_->GetLatestPoolInfo(
            network_id,
            pool_index_,
            &pool_info)) {
        // 根据数据库更新内存中的 tx_pool 状态
        if (latest_height_ == common::kInvalidUint64 || latest_height_ < pool_info.height()) {
            latest_height_ = pool_info.height();
            latest_hash_ = pool_info.hash();
            synced_height_ = pool_info.synced_height();
            latest_timestamp_ = pool_info.timestamp();
            prev_synced_height_ = synced_height_;
            to_sync_max_height_ = latest_height_;
            ZJC_DEBUG("init latest pool info shard: %u, pool %lu, init height: %lu",
                network_id, pool_index_, latest_height_);
        }
    }
}

void TxPool::UpdateSyncedHeight() {
    if (!height_tree_ptr_) {
        return;
    }

    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr_->Valid(synced_height_ + 1)) {
            break;
        }
    }
}

uint64_t TxPool::UpdateLatestInfo(
        uint64_t height,
        const std::string& hash,
        const std::string& prehash,
        const uint64_t timestamp) {
    if (!kv_sync_) {
        return common::kInvalidUint64;
    }
    
    CheckThreadIdValid();
    auto tmp_height_tree_ptr = height_tree_ptr_;
    if (!tmp_height_tree_ptr) {
        InitHeightTree();
    }

    tmp_height_tree_ptr = height_tree_ptr_;
    if (tmp_height_tree_ptr) {
        ZJC_DEBUG("success set height, net: %u, pool: %u, height: %lu",
            common::GlobalInfo::Instance()->network_id(), pool_index_, height);
        tmp_height_tree_ptr->Set(height);
    }

    if (latest_height_ == common::kInvalidUint64 || latest_height_ < height) {
        latest_height_ = height;
        latest_hash_ = hash;
        latest_timestamp_ = timestamp;
    }

    if (to_sync_max_height_ == common::kInvalidUint64 || to_sync_max_height_ < latest_height_) {
        to_sync_max_height_ = latest_height_;
    }

    if (height > synced_height_) {
        checked_height_with_prehash_[height] = prehash;
        // CHECK_MEMORY_SIZE(checked_height_with_prehash_);
    }

    if (synced_height_ + 1 == height) {
        synced_height_ = height;
        auto iter = checked_height_with_prehash_.begin();
        while (iter != checked_height_with_prehash_.end()) {
            if (iter->first < synced_height_) {
                iter = checked_height_with_prehash_.erase(iter);
            } else {
                ++iter;
            }
        }

        UpdateSyncedHeight();
        if (prev_synced_height_ < synced_height_) {
            prev_synced_height_ = synced_height_;
        }
    } else {
        SyncBlock();
    }

    ZJC_DEBUG("pool index: %d, new height: %lu, new synced height: %lu, prev_synced_height_: %lu, to_sync_max_height_: %lu, latest height: %lu",
        pool_index_, height, synced_height_, prev_synced_height_, to_sync_max_height_, latest_height_);
    return synced_height_;
}

void TxPool::SyncBlock() {
    if (height_tree_ptr_ == nullptr) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    for (; prev_synced_height_ < to_sync_max_height_ &&
            (prev_synced_height_ < synced_height_ + 64);
            ++prev_synced_height_) {
        if (!height_tree_ptr_->Valid(prev_synced_height_ + 1)) {
            ZJC_DEBUG("now add sync height 1, %u_%u_%lu", 
                net_id,
                pool_index_,
                prev_synced_height_ + 1);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                prev_synced_height_ + 1,
                sync::kSyncHighest);
        }
    }
}

void TxPool::ConsensusAddTxs(const pools::TxItemPtr& tx_ptr) {
    if (tx_ptr->address_info->nonce() >= tx_ptr->tx_info->nonce()) {
        return;
    }

    if (!IsUserTransaction(tx_ptr->tx_info->step())) {
        return;
    }

    CheckThreadIdValid();
    if (consensus_added_txs_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_WARN("add failed extend %u, %u, all valid: %u", 
            consensus_added_txs_.size(), common::GlobalInfo::Instance()->each_tx_pool_max_txs(), all_tx_size());
        return;
    }

    if (tx_ptr->tx_key.empty()) {
        ZJC_WARN("add failed unique hash empty: %d", tx_ptr->tx_info->step());
        tx_ptr->tx_key = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    ZJC_DEBUG("trace tx pool: %d, sync add tx addr: %s, nonce: %lu", 
        pool_index_,
        common::Encode::HexEncode(tx_ptr->address_info->addr()).c_str(), 
        tx_ptr->tx_info->nonce());
    consensus_added_txs_.push(tx_ptr);
}

}  // namespace pools

}  // namespace shardora
