#include "pools/to_txs_pools.h"

#include "common/global_info.h"
#include "common/user_property_key_define.h"
#include "network/network_utils.h
#include "network/route.h"
#include "transport/processor.h"

namespace zjchain {

namespace pools {

ToTxsPools::ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id) : db_(db), local_id_(local_id) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    LoadLatestHeights();
    address_map_.Init(10240, 16);
    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&ToTxsPools::ConsensusTimerMessage, this, std::placeholders::_1));
}

ToTxsPools::~ToTxsPools() {}

void ToTxsPools::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    if (!CheckLeaderValid(local_id_)) {
        return;
    }

    for (uint32_t i = 2; i < common::GlobalInfo::Instance()->consensus_shard_count(); ++i) {
        auto to_tx_msg_ptr = std::make_shared<transport::TransportMessage>();
        to_tx_msg_ptr->thread_idx = msg_ptr->thread_idx;
        auto& msg = to_tx_msg_ptr->header;
        auto& to_heights = *msg.mutable_to_tx_heights();
        if (LeaderCreateToTx(i, &to_heights) != kPoolsSuccess) {
            continue;
        }

        // broadcast to followers
        network::Route::Instance()->Send(to_tx_msg_ptr);
    }
}

void ToTxsPools::OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members) {

}

bool ToTxsPools::CheckLeaderValid(const std::string& id) {
    return false;
}

void ToTxsPools::NewBlock(const block::protobuf::Block& block, db::DbWriteBach& db_batch) {
    if (block.network_id() != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    const auto& tx_list = block.tx_list();
    if (tx_list.empty()) {
        return;
    }
    
    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].step() == pools::protobuf::kNormalTo) {
            // remove each less height
            HandleNormalToTx(block.height(), tx_list[i], db_batch);
            continue;
        }

        if (tx_list[i].step() != pools::protobuf::kNormalFrom) {
            continue;
        }

        if (tx_list[i].amount() <= 0) {
            continue;
        }

        uint32_t sharding_id = common::kInvalidUint32;
        auto addr_info = GetAddressInfo(tx_list[i].to());
        if (addr_info == nullptr) {
            sharding_id = network::kRootCongressNetworkId;
        } else {
            sharding_id = addr_info->sharding_id();
        }

        auto handled_iter = handled_map_.find(sharding_id);
        if (handled_iter != handled_map_.end()) {
            if (handled_iter->second->heights(block.pool_index()) >= block.height()) {
                continue;
            }
        }

        auto net_iter = network_txs_pools_.find(sharding_id);
        if (net_iter == network_txs_pools_.end()) {
            PoolMap pool_map;
            network_txs_pools_[sharding_id] = pool_map;
            net_iter = network_txs_pools_.find(sharding_id);
        }

        auto pool_iter = net_iter->second.find(block.pool_index());
        if (pool_iter == net_iter->second.end()) {
            HeightMap height_map;
            net_iter->second[block.pool_index()] = height_map;
            pool_iter = net_iter->second.find(block.pool_index());
        }

        auto height_iter = pool_iter->second.find(block.height());
        if (height_iter == pool_iter->second.end()) {
            TxMap tx_map;
            pool_iter->second[block.height()] = tx_map;
            height_iter = pool_iter->second.find(block.height());
        }

        height_iter->second[tx_list[i].to()] += tx_list[i].amount();
    }
}

void ToTxsPools::HandleNormalToTx(
        uint64_t block_height,
        const block::protobuf::BlockTx& tx_info,
        db::DbWriteBach& db_batch) {
    if (tx_info.storages_size() <= 0) {
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ToTxHeights>();
    auto& heights = *heights_ptr;
    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        if (tx_info.storages(i).key() == protos::kNormalTos) {
            if (!prefix_db_->GetToTxsHeights(tx_info.storages(i).val_hash(), &heights)) {
                return;
            }
            
            break;
        }
    }

    if (heights.heights_size() != common::kImmutablePoolSize) {
        ZJC_ERROR("invalid heights size: %d, %d",
            heights.heights_size(), common::kImmutablePoolSize);
        return;
    }

    auto handled_iter = handled_map_.find(heights.sharding_id());
    if (handled_iter != handled_map_.end()) {
        if (handled_iter->second->block_height() >= block_height) {
            return;
        }
    }

    handled_map_[heights.sharding_id()] = heights_ptr;
    prefix_db_->SaveLatestToTxsHeights(heights, db_batch);
    auto net_iter = network_txs_pools_.find(heights.sharding_id());
    if (net_iter == network_txs_pools_.end()) {
        return;
    }

    for (int32_t i = 0; i < heights.heights_size(); ++i) {
        auto pool_iter = net_iter->second.find(i);
        if (pool_iter == net_iter->second.end()) {
            continue;
        }

        auto height_iter = pool_iter->second.begin();
        while (height_iter != pool_iter->second.end()) {
            if (height_iter->first > heights.heights(i)) {
                break;
            }

            pool_iter->second.erase(height_iter++);
        }
    }
}

void ToTxsPools::LoadLatestHeights() {
    for (uint32_t i = network::kRootCongressNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        auto heights_ptr = std::make_shared<pools::protobuf::ToTxHeights>();
        pools::protobuf::ToTxHeights& to_heights = *heights_ptr;
        if (!prefix_db_->GetLatestToTxsHeights(i, &to_heights)) {
            continue;
        }

        handled_map_[i] = heights_ptr;
    }
}

std::shared_ptr<address::protobuf::AddressInfo> ToTxsPools::GetAddressInfo(
        const std::string& addr) {
    // first get from cache
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    if (address_map_.get(addr, &address_info)) {
        return address_info;
    }

    // get from db and add to memory cache
    address_info = prefix_db_->GetAddressInfo(addr);
    if (address_info != nullptr) {
        address_map_.add(addr, address_info);
    }

    return address_info;
}

int ToTxsPools::LeaderCreateToTx(uint32_t sharding_id, pools::protobuf::ToTxHeights& to_heights) {
    pools::protobuf::ToTxMessage to_tx;
    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        return kPoolsError;
    }

    auto handled_iter = handled_map_.find(sharding_id);
    to_heights.set_sharding_id(sharding_id);
    std::map<std::string, uint64_t> acc_amount_map;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto pool_iter = net_iter->second.find(i);
        if (pool_iter == net_iter->second.end()) {
            if (handled_iter == handled_map_.end()) {
                to_heights.add_heights(0);
            } else {
                to_heights.add_heights(handled_iter->second->heights(i));
            }

            continue;
        }

        auto r_height_iter = pool_iter->second.rbegin();
        to_heights.add_heights(r_height_iter->first);
        for (auto hiter = pool_iter->second.begin();
                hiter != pool_iter->second.end(); ++hiter) {
            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    acc_amount_map[to_iter->first] = to_iter->second;
                } else {
                    amount_iter->second += to_iter->second;
                }
            }
        }
    }

    std::string str_for_hash;
    str_for_hash.reserve(common::kImmutablePoolSize * 8 + acc_amount_map.size() * 48);
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        uint64_t height = to_heights.heights(i);
        str_for_hash.append((char*)&height, sizeof(height));
    }

    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        str_for_hash.append(iter->first);
        str_for_hash.append((char*)&iter->second, sizeof(iter->second));
        auto to_item = to_tx.add_tos();
        to_item->set_des(iter->first);
        to_item->set_amount(iter->second);
    }

    auto tos_hash = common::Hash::keccak256(str_for_hash);
    to_tx.set_heights_hash(tos_hash);
    auto val = to_tx.SerializeAsString();
    to_heights.set_tos_hash(tos_hash);
    prefix_db_->SaveTemporaryKv(tos_hash, val);
    tx->set_key(protos::kNormalTos);
    tx->set_value(to_heights.SerializeAsString());
    tx->set_pubkey("");
    tx->set_to("");
    tx->set_step(pools::protobuf::kNormalTo);
    auto gid = common::Hash::keccak256(tos_hash + std::to_string(sharding_id));
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_gid(gid);
    return kPoolsSuccess;
}

int ToTxsPools::BackupCreateToTx(
        uint32_t sharding_id,
        const pools::protobuf::ToTxHeights& leader_to_heights,
        pools::protobuf::TxMessage* tx) {
    pools::protobuf::ToTxMessage to_tx;
    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        return kPoolsError;
    }

    if (leader_to_heights.heights_size() != common::kImmutablePoolSize) {
        return kPoolsError;
    }

    auto handled_iter = handled_map_.find(sharding_id);
    pools::protobuf::ToTxHeights to_heights;
    to_heights.set_sharding_id(sharding_id);
    std::map<std::string, uint64_t> acc_amount_map;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto pool_iter = net_iter->second.find(i);
        if (pool_iter == net_iter->second.end()) {
            if (handled_iter == handled_map_.end()) {
                to_heights.add_heights(0);
            } else {
                to_heights.add_heights(handled_iter->second->heights(i));
            }

            continue;
        }

        to_heights.add_heights(leader_to_heights.heights(i));
        for (auto hiter = pool_iter->second.begin();
                hiter != pool_iter->second.end(); ++hiter) {
            if (hiter->first > leader_to_heights.heights(i)) {
                break;
            }

            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    acc_amount_map[to_iter->first] = to_iter->second;
                } else {
                    amount_iter->second += to_iter->second;
                }
            }
        }
    }

    std::string str_for_hash;
    str_for_hash.reserve(common::kImmutablePoolSize * 8 + acc_amount_map.size() * 48);
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        uint64_t height = to_heights.heights(i);
        str_for_hash.append((char*)&height, sizeof(height));
    }

    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        str_for_hash.append(iter->first);
        str_for_hash.append((char*)&iter->second, sizeof(iter->second));
        auto to_item = to_tx.add_tos();
        to_item->set_des(iter->first);
        to_item->set_amount(iter->second);
    }

    auto tos_hash = common::Hash::keccak256(str_for_hash);
    to_tx.set_heights_hash(tos_hash);
    auto val = to_tx.SerializeAsString();
//     auto tos_hash = common::Hash::keccak256(val);
    to_heights.set_tos_hash(tos_hash);
    prefix_db_->SaveTemporaryKv(tos_hash, val);
    tx->set_key(protos::kNormalTos);
    tx->set_value(to_heights.SerializeAsString());
    tx->set_pubkey("");
    tx->set_to("");
    tx->set_step(pools::protobuf::kNormalTo);
    auto gid = common::Hash::keccak256(tos_hash + std::to_string(sharding_id));
    tx->set_gas_limit(0);
    tx->set_amount(0);
    tx->set_gas_price(common::kBuildinTransactionGasPrice);
    tx->set_gid(gid);
    return kPoolsSuccess;
}

};  // namespace pools

};  // namespace zjchain
